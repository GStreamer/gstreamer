

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 7.00.0500 */
/* at Fri Jun 17 10:43:51 2011
 */
/* Compiler settings for .\win\DeckLinkAPI.idl:
    Oicf, W1, Zp8, env=Win32 (32b run)
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
//@@MIDL_FILE_HEADING(  )

#pragma warning( disable: 4049 )  /* more than 64k source lines */


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif // __RPCNDR_H_VERSION__


#ifndef __DeckLinkAPI_h__
#define __DeckLinkAPI_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __IDeckLinkVideoOutputCallback_FWD_DEFINED__
#define __IDeckLinkVideoOutputCallback_FWD_DEFINED__
typedef interface IDeckLinkVideoOutputCallback IDeckLinkVideoOutputCallback;
#endif 	/* __IDeckLinkVideoOutputCallback_FWD_DEFINED__ */


#ifndef __IDeckLinkInputCallback_FWD_DEFINED__
#define __IDeckLinkInputCallback_FWD_DEFINED__
typedef interface IDeckLinkInputCallback IDeckLinkInputCallback;
#endif 	/* __IDeckLinkInputCallback_FWD_DEFINED__ */


#ifndef __IDeckLinkMemoryAllocator_FWD_DEFINED__
#define __IDeckLinkMemoryAllocator_FWD_DEFINED__
typedef interface IDeckLinkMemoryAllocator IDeckLinkMemoryAllocator;
#endif 	/* __IDeckLinkMemoryAllocator_FWD_DEFINED__ */


#ifndef __IDeckLinkAudioOutputCallback_FWD_DEFINED__
#define __IDeckLinkAudioOutputCallback_FWD_DEFINED__
typedef interface IDeckLinkAudioOutputCallback IDeckLinkAudioOutputCallback;
#endif 	/* __IDeckLinkAudioOutputCallback_FWD_DEFINED__ */


#ifndef __IDeckLinkIterator_FWD_DEFINED__
#define __IDeckLinkIterator_FWD_DEFINED__
typedef interface IDeckLinkIterator IDeckLinkIterator;
#endif 	/* __IDeckLinkIterator_FWD_DEFINED__ */


#ifndef __IDeckLinkAPIInformation_FWD_DEFINED__
#define __IDeckLinkAPIInformation_FWD_DEFINED__
typedef interface IDeckLinkAPIInformation IDeckLinkAPIInformation;
#endif 	/* __IDeckLinkAPIInformation_FWD_DEFINED__ */


#ifndef __IDeckLinkDisplayModeIterator_FWD_DEFINED__
#define __IDeckLinkDisplayModeIterator_FWD_DEFINED__
typedef interface IDeckLinkDisplayModeIterator IDeckLinkDisplayModeIterator;
#endif 	/* __IDeckLinkDisplayModeIterator_FWD_DEFINED__ */


#ifndef __IDeckLinkDisplayMode_FWD_DEFINED__
#define __IDeckLinkDisplayMode_FWD_DEFINED__
typedef interface IDeckLinkDisplayMode IDeckLinkDisplayMode;
#endif 	/* __IDeckLinkDisplayMode_FWD_DEFINED__ */


#ifndef __IDeckLink_FWD_DEFINED__
#define __IDeckLink_FWD_DEFINED__
typedef interface IDeckLink IDeckLink;
#endif 	/* __IDeckLink_FWD_DEFINED__ */


#ifndef __IDeckLinkOutput_FWD_DEFINED__
#define __IDeckLinkOutput_FWD_DEFINED__
typedef interface IDeckLinkOutput IDeckLinkOutput;
#endif 	/* __IDeckLinkOutput_FWD_DEFINED__ */


#ifndef __IDeckLinkInput_FWD_DEFINED__
#define __IDeckLinkInput_FWD_DEFINED__
typedef interface IDeckLinkInput IDeckLinkInput;
#endif 	/* __IDeckLinkInput_FWD_DEFINED__ */


#ifndef __IDeckLinkTimecode_FWD_DEFINED__
#define __IDeckLinkTimecode_FWD_DEFINED__
typedef interface IDeckLinkTimecode IDeckLinkTimecode;
#endif 	/* __IDeckLinkTimecode_FWD_DEFINED__ */


#ifndef __IDeckLinkVideoFrame_FWD_DEFINED__
#define __IDeckLinkVideoFrame_FWD_DEFINED__
typedef interface IDeckLinkVideoFrame IDeckLinkVideoFrame;
#endif 	/* __IDeckLinkVideoFrame_FWD_DEFINED__ */


#ifndef __IDeckLinkMutableVideoFrame_FWD_DEFINED__
#define __IDeckLinkMutableVideoFrame_FWD_DEFINED__
typedef interface IDeckLinkMutableVideoFrame IDeckLinkMutableVideoFrame;
#endif 	/* __IDeckLinkMutableVideoFrame_FWD_DEFINED__ */


#ifndef __IDeckLinkVideoFrame3DExtensions_FWD_DEFINED__
#define __IDeckLinkVideoFrame3DExtensions_FWD_DEFINED__
typedef interface IDeckLinkVideoFrame3DExtensions IDeckLinkVideoFrame3DExtensions;
#endif 	/* __IDeckLinkVideoFrame3DExtensions_FWD_DEFINED__ */


#ifndef __IDeckLinkVideoInputFrame_FWD_DEFINED__
#define __IDeckLinkVideoInputFrame_FWD_DEFINED__
typedef interface IDeckLinkVideoInputFrame IDeckLinkVideoInputFrame;
#endif 	/* __IDeckLinkVideoInputFrame_FWD_DEFINED__ */


#ifndef __IDeckLinkVideoFrameAncillary_FWD_DEFINED__
#define __IDeckLinkVideoFrameAncillary_FWD_DEFINED__
typedef interface IDeckLinkVideoFrameAncillary IDeckLinkVideoFrameAncillary;
#endif 	/* __IDeckLinkVideoFrameAncillary_FWD_DEFINED__ */


#ifndef __IDeckLinkAudioInputPacket_FWD_DEFINED__
#define __IDeckLinkAudioInputPacket_FWD_DEFINED__
typedef interface IDeckLinkAudioInputPacket IDeckLinkAudioInputPacket;
#endif 	/* __IDeckLinkAudioInputPacket_FWD_DEFINED__ */


#ifndef __IDeckLinkScreenPreviewCallback_FWD_DEFINED__
#define __IDeckLinkScreenPreviewCallback_FWD_DEFINED__
typedef interface IDeckLinkScreenPreviewCallback IDeckLinkScreenPreviewCallback;
#endif 	/* __IDeckLinkScreenPreviewCallback_FWD_DEFINED__ */


#ifndef __IDeckLinkGLScreenPreviewHelper_FWD_DEFINED__
#define __IDeckLinkGLScreenPreviewHelper_FWD_DEFINED__
typedef interface IDeckLinkGLScreenPreviewHelper IDeckLinkGLScreenPreviewHelper;
#endif 	/* __IDeckLinkGLScreenPreviewHelper_FWD_DEFINED__ */


#ifndef __IDeckLinkConfiguration_FWD_DEFINED__
#define __IDeckLinkConfiguration_FWD_DEFINED__
typedef interface IDeckLinkConfiguration IDeckLinkConfiguration;
#endif 	/* __IDeckLinkConfiguration_FWD_DEFINED__ */


#ifndef __IDeckLinkAttributes_FWD_DEFINED__
#define __IDeckLinkAttributes_FWD_DEFINED__
typedef interface IDeckLinkAttributes IDeckLinkAttributes;
#endif 	/* __IDeckLinkAttributes_FWD_DEFINED__ */


#ifndef __IDeckLinkKeyer_FWD_DEFINED__
#define __IDeckLinkKeyer_FWD_DEFINED__
typedef interface IDeckLinkKeyer IDeckLinkKeyer;
#endif 	/* __IDeckLinkKeyer_FWD_DEFINED__ */


#ifndef __IDeckLinkVideoConversion_FWD_DEFINED__
#define __IDeckLinkVideoConversion_FWD_DEFINED__
typedef interface IDeckLinkVideoConversion IDeckLinkVideoConversion;
#endif 	/* __IDeckLinkVideoConversion_FWD_DEFINED__ */


#ifndef __IDeckLinkDeckControlStatusCallback_FWD_DEFINED__
#define __IDeckLinkDeckControlStatusCallback_FWD_DEFINED__
typedef interface IDeckLinkDeckControlStatusCallback IDeckLinkDeckControlStatusCallback;
#endif 	/* __IDeckLinkDeckControlStatusCallback_FWD_DEFINED__ */


#ifndef __IDeckLinkDeckControl_FWD_DEFINED__
#define __IDeckLinkDeckControl_FWD_DEFINED__
typedef interface IDeckLinkDeckControl IDeckLinkDeckControl;
#endif 	/* __IDeckLinkDeckControl_FWD_DEFINED__ */


#ifndef __CDeckLinkIterator_FWD_DEFINED__
#define __CDeckLinkIterator_FWD_DEFINED__

#ifdef __cplusplus
typedef class CDeckLinkIterator CDeckLinkIterator;
#else
typedef struct CDeckLinkIterator CDeckLinkIterator;
#endif /* __cplusplus */

#endif 	/* __CDeckLinkIterator_FWD_DEFINED__ */


#ifndef __CDeckLinkAPIInformation_FWD_DEFINED__
#define __CDeckLinkAPIInformation_FWD_DEFINED__

#ifdef __cplusplus
typedef class CDeckLinkAPIInformation CDeckLinkAPIInformation;
#else
typedef struct CDeckLinkAPIInformation CDeckLinkAPIInformation;
#endif /* __cplusplus */

#endif 	/* __CDeckLinkAPIInformation_FWD_DEFINED__ */


#ifndef __CDeckLinkGLScreenPreviewHelper_FWD_DEFINED__
#define __CDeckLinkGLScreenPreviewHelper_FWD_DEFINED__

#ifdef __cplusplus
typedef class CDeckLinkGLScreenPreviewHelper CDeckLinkGLScreenPreviewHelper;
#else
typedef struct CDeckLinkGLScreenPreviewHelper CDeckLinkGLScreenPreviewHelper;
#endif /* __cplusplus */

#endif 	/* __CDeckLinkGLScreenPreviewHelper_FWD_DEFINED__ */


#ifndef __CDeckLinkVideoConversion_FWD_DEFINED__
#define __CDeckLinkVideoConversion_FWD_DEFINED__

#ifdef __cplusplus
typedef class CDeckLinkVideoConversion CDeckLinkVideoConversion;
#else
typedef struct CDeckLinkVideoConversion CDeckLinkVideoConversion;
#endif /* __cplusplus */

#endif 	/* __CDeckLinkVideoConversion_FWD_DEFINED__ */


#ifndef __IDeckLinkDeckControl_v7_9_FWD_DEFINED__
#define __IDeckLinkDeckControl_v7_9_FWD_DEFINED__
typedef interface IDeckLinkDeckControl_v7_9 IDeckLinkDeckControl_v7_9;
#endif 	/* __IDeckLinkDeckControl_v7_9_FWD_DEFINED__ */


#ifndef __IDeckLinkDisplayModeIterator_v7_6_FWD_DEFINED__
#define __IDeckLinkDisplayModeIterator_v7_6_FWD_DEFINED__
typedef interface IDeckLinkDisplayModeIterator_v7_6 IDeckLinkDisplayModeIterator_v7_6;
#endif 	/* __IDeckLinkDisplayModeIterator_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkDisplayMode_v7_6_FWD_DEFINED__
#define __IDeckLinkDisplayMode_v7_6_FWD_DEFINED__
typedef interface IDeckLinkDisplayMode_v7_6 IDeckLinkDisplayMode_v7_6;
#endif 	/* __IDeckLinkDisplayMode_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkOutput_v7_6_FWD_DEFINED__
#define __IDeckLinkOutput_v7_6_FWD_DEFINED__
typedef interface IDeckLinkOutput_v7_6 IDeckLinkOutput_v7_6;
#endif 	/* __IDeckLinkOutput_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkInput_v7_6_FWD_DEFINED__
#define __IDeckLinkInput_v7_6_FWD_DEFINED__
typedef interface IDeckLinkInput_v7_6 IDeckLinkInput_v7_6;
#endif 	/* __IDeckLinkInput_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkTimecode_v7_6_FWD_DEFINED__
#define __IDeckLinkTimecode_v7_6_FWD_DEFINED__
typedef interface IDeckLinkTimecode_v7_6 IDeckLinkTimecode_v7_6;
#endif 	/* __IDeckLinkTimecode_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkVideoFrame_v7_6_FWD_DEFINED__
#define __IDeckLinkVideoFrame_v7_6_FWD_DEFINED__
typedef interface IDeckLinkVideoFrame_v7_6 IDeckLinkVideoFrame_v7_6;
#endif 	/* __IDeckLinkVideoFrame_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkMutableVideoFrame_v7_6_FWD_DEFINED__
#define __IDeckLinkMutableVideoFrame_v7_6_FWD_DEFINED__
typedef interface IDeckLinkMutableVideoFrame_v7_6 IDeckLinkMutableVideoFrame_v7_6;
#endif 	/* __IDeckLinkMutableVideoFrame_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkVideoInputFrame_v7_6_FWD_DEFINED__
#define __IDeckLinkVideoInputFrame_v7_6_FWD_DEFINED__
typedef interface IDeckLinkVideoInputFrame_v7_6 IDeckLinkVideoInputFrame_v7_6;
#endif 	/* __IDeckLinkVideoInputFrame_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkScreenPreviewCallback_v7_6_FWD_DEFINED__
#define __IDeckLinkScreenPreviewCallback_v7_6_FWD_DEFINED__
typedef interface IDeckLinkScreenPreviewCallback_v7_6 IDeckLinkScreenPreviewCallback_v7_6;
#endif 	/* __IDeckLinkScreenPreviewCallback_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkGLScreenPreviewHelper_v7_6_FWD_DEFINED__
#define __IDeckLinkGLScreenPreviewHelper_v7_6_FWD_DEFINED__
typedef interface IDeckLinkGLScreenPreviewHelper_v7_6 IDeckLinkGLScreenPreviewHelper_v7_6;
#endif 	/* __IDeckLinkGLScreenPreviewHelper_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkVideoConversion_v7_6_FWD_DEFINED__
#define __IDeckLinkVideoConversion_v7_6_FWD_DEFINED__
typedef interface IDeckLinkVideoConversion_v7_6 IDeckLinkVideoConversion_v7_6;
#endif 	/* __IDeckLinkVideoConversion_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkConfiguration_v7_6_FWD_DEFINED__
#define __IDeckLinkConfiguration_v7_6_FWD_DEFINED__
typedef interface IDeckLinkConfiguration_v7_6 IDeckLinkConfiguration_v7_6;
#endif 	/* __IDeckLinkConfiguration_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkVideoOutputCallback_v7_6_FWD_DEFINED__
#define __IDeckLinkVideoOutputCallback_v7_6_FWD_DEFINED__
typedef interface IDeckLinkVideoOutputCallback_v7_6 IDeckLinkVideoOutputCallback_v7_6;
#endif 	/* __IDeckLinkVideoOutputCallback_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkInputCallback_v7_6_FWD_DEFINED__
#define __IDeckLinkInputCallback_v7_6_FWD_DEFINED__
typedef interface IDeckLinkInputCallback_v7_6 IDeckLinkInputCallback_v7_6;
#endif 	/* __IDeckLinkInputCallback_v7_6_FWD_DEFINED__ */


#ifndef __CDeckLinkGLScreenPreviewHelper_v7_6_FWD_DEFINED__
#define __CDeckLinkGLScreenPreviewHelper_v7_6_FWD_DEFINED__

#ifdef __cplusplus
typedef class CDeckLinkGLScreenPreviewHelper_v7_6 CDeckLinkGLScreenPreviewHelper_v7_6;
#else
typedef struct CDeckLinkGLScreenPreviewHelper_v7_6 CDeckLinkGLScreenPreviewHelper_v7_6;
#endif /* __cplusplus */

#endif 	/* __CDeckLinkGLScreenPreviewHelper_v7_6_FWD_DEFINED__ */


#ifndef __CDeckLinkVideoConversion_v7_6_FWD_DEFINED__
#define __CDeckLinkVideoConversion_v7_6_FWD_DEFINED__

#ifdef __cplusplus
typedef class CDeckLinkVideoConversion_v7_6 CDeckLinkVideoConversion_v7_6;
#else
typedef struct CDeckLinkVideoConversion_v7_6 CDeckLinkVideoConversion_v7_6;
#endif /* __cplusplus */

#endif 	/* __CDeckLinkVideoConversion_v7_6_FWD_DEFINED__ */


#ifndef __IDeckLinkInputCallback_v7_3_FWD_DEFINED__
#define __IDeckLinkInputCallback_v7_3_FWD_DEFINED__
typedef interface IDeckLinkInputCallback_v7_3 IDeckLinkInputCallback_v7_3;
#endif 	/* __IDeckLinkInputCallback_v7_3_FWD_DEFINED__ */


#ifndef __IDeckLinkOutput_v7_3_FWD_DEFINED__
#define __IDeckLinkOutput_v7_3_FWD_DEFINED__
typedef interface IDeckLinkOutput_v7_3 IDeckLinkOutput_v7_3;
#endif 	/* __IDeckLinkOutput_v7_3_FWD_DEFINED__ */


#ifndef __IDeckLinkInput_v7_3_FWD_DEFINED__
#define __IDeckLinkInput_v7_3_FWD_DEFINED__
typedef interface IDeckLinkInput_v7_3 IDeckLinkInput_v7_3;
#endif 	/* __IDeckLinkInput_v7_3_FWD_DEFINED__ */


#ifndef __IDeckLinkVideoInputFrame_v7_3_FWD_DEFINED__
#define __IDeckLinkVideoInputFrame_v7_3_FWD_DEFINED__
typedef interface IDeckLinkVideoInputFrame_v7_3 IDeckLinkVideoInputFrame_v7_3;
#endif 	/* __IDeckLinkVideoInputFrame_v7_3_FWD_DEFINED__ */


#ifndef __IDeckLinkDisplayModeIterator_v7_1_FWD_DEFINED__
#define __IDeckLinkDisplayModeIterator_v7_1_FWD_DEFINED__
typedef interface IDeckLinkDisplayModeIterator_v7_1 IDeckLinkDisplayModeIterator_v7_1;
#endif 	/* __IDeckLinkDisplayModeIterator_v7_1_FWD_DEFINED__ */


#ifndef __IDeckLinkDisplayMode_v7_1_FWD_DEFINED__
#define __IDeckLinkDisplayMode_v7_1_FWD_DEFINED__
typedef interface IDeckLinkDisplayMode_v7_1 IDeckLinkDisplayMode_v7_1;
#endif 	/* __IDeckLinkDisplayMode_v7_1_FWD_DEFINED__ */


#ifndef __IDeckLinkVideoFrame_v7_1_FWD_DEFINED__
#define __IDeckLinkVideoFrame_v7_1_FWD_DEFINED__
typedef interface IDeckLinkVideoFrame_v7_1 IDeckLinkVideoFrame_v7_1;
#endif 	/* __IDeckLinkVideoFrame_v7_1_FWD_DEFINED__ */


#ifndef __IDeckLinkVideoInputFrame_v7_1_FWD_DEFINED__
#define __IDeckLinkVideoInputFrame_v7_1_FWD_DEFINED__
typedef interface IDeckLinkVideoInputFrame_v7_1 IDeckLinkVideoInputFrame_v7_1;
#endif 	/* __IDeckLinkVideoInputFrame_v7_1_FWD_DEFINED__ */


#ifndef __IDeckLinkAudioInputPacket_v7_1_FWD_DEFINED__
#define __IDeckLinkAudioInputPacket_v7_1_FWD_DEFINED__
typedef interface IDeckLinkAudioInputPacket_v7_1 IDeckLinkAudioInputPacket_v7_1;
#endif 	/* __IDeckLinkAudioInputPacket_v7_1_FWD_DEFINED__ */


#ifndef __IDeckLinkVideoOutputCallback_v7_1_FWD_DEFINED__
#define __IDeckLinkVideoOutputCallback_v7_1_FWD_DEFINED__
typedef interface IDeckLinkVideoOutputCallback_v7_1 IDeckLinkVideoOutputCallback_v7_1;
#endif 	/* __IDeckLinkVideoOutputCallback_v7_1_FWD_DEFINED__ */


#ifndef __IDeckLinkInputCallback_v7_1_FWD_DEFINED__
#define __IDeckLinkInputCallback_v7_1_FWD_DEFINED__
typedef interface IDeckLinkInputCallback_v7_1 IDeckLinkInputCallback_v7_1;
#endif 	/* __IDeckLinkInputCallback_v7_1_FWD_DEFINED__ */


#ifndef __IDeckLinkOutput_v7_1_FWD_DEFINED__
#define __IDeckLinkOutput_v7_1_FWD_DEFINED__
typedef interface IDeckLinkOutput_v7_1 IDeckLinkOutput_v7_1;
#endif 	/* __IDeckLinkOutput_v7_1_FWD_DEFINED__ */


#ifndef __IDeckLinkInput_v7_1_FWD_DEFINED__
#define __IDeckLinkInput_v7_1_FWD_DEFINED__
typedef interface IDeckLinkInput_v7_1 IDeckLinkInput_v7_1;
#endif 	/* __IDeckLinkInput_v7_1_FWD_DEFINED__ */


/* header files for imported files */
#include "unknwn.h"

#ifdef __cplusplus
extern "C"{
#endif 



#ifndef __DeckLinkAPI_LIBRARY_DEFINED__
#define __DeckLinkAPI_LIBRARY_DEFINED__

/* library DeckLinkAPI */
/* [helpstring][version][uuid] */ 

typedef LONGLONG BMDTimeValue;

typedef LONGLONG BMDTimeScale;

typedef unsigned long BMDTimecodeBCD;

typedef unsigned long BMDTimecodeUserBits;

typedef unsigned long BMDDisplayModeFlags;
typedef unsigned long BMDFrameFlags;
typedef unsigned long BMDVideoInputFlags;
typedef unsigned long BMDVideoInputFormatChangedEvents;
typedef unsigned long BMDDetectedVideoInputFormatFlags;
typedef unsigned long BMDTimecodeFlags;
typedef unsigned long BMDAnalogVideoFlags;
typedef unsigned long BMDDeckControlStatusFlags;
typedef unsigned long BMDDeckControlExportModeOpsFlags;
#if 0
typedef enum _BMDDisplayModeFlags BMDDisplayModeFlags;

typedef enum _BMDFrameFlags BMDFrameFlags;

typedef enum _BMDVideoInputFlags BMDVideoInputFlags;

typedef enum _BMDVideoInputFormatChangedEvents BMDVideoInputFormatChangedEvents;

typedef enum _BMDDetectedVideoInputFormatFlags BMDDetectedVideoInputFormatFlags;

typedef enum _BMDTimecodeFlags BMDTimecodeFlags;

typedef enum _BMDAnalogVideoFlags BMDAnalogVideoFlags;

typedef enum _BMDDeckControlStatusFlags BMDDeckControlStatusFlags;

typedef enum _BMDDeckControlExportModeOpsFlags BMDDeckControlExportModeOpsFlags;

#endif
typedef /* [v1_enum] */ 
enum _BMDDisplayMode
    {	bmdModeNTSC	= 0x6e747363,
	bmdModeNTSC2398	= 0x6e743233,
	bmdModePAL	= 0x70616c20,
	bmdModeNTSCp	= 0x6e747370,
	bmdModePALp	= 0x70616c70,
	bmdModeHD1080p2398	= 0x32337073,
	bmdModeHD1080p24	= 0x32347073,
	bmdModeHD1080p25	= 0x48703235,
	bmdModeHD1080p2997	= 0x48703239,
	bmdModeHD1080p30	= 0x48703330,
	bmdModeHD1080i50	= 0x48693530,
	bmdModeHD1080i5994	= 0x48693539,
	bmdModeHD1080i6000	= 0x48693630,
	bmdModeHD1080p50	= 0x48703530,
	bmdModeHD1080p5994	= 0x48703539,
	bmdModeHD1080p6000	= 0x48703630,
	bmdModeHD720p50	= 0x68703530,
	bmdModeHD720p5994	= 0x68703539,
	bmdModeHD720p60	= 0x68703630,
	bmdMode2k2398	= 0x326b3233,
	bmdMode2k24	= 0x326b3234,
	bmdMode2k25	= 0x326b3235
    } 	BMDDisplayMode;

typedef /* [v1_enum] */ 
enum _BMDFieldDominance
    {	bmdUnknownFieldDominance	= 0,
	bmdLowerFieldFirst	= 0x6c6f7772,
	bmdUpperFieldFirst	= 0x75707072,
	bmdProgressiveFrame	= 0x70726f67,
	bmdProgressiveSegmentedFrame	= 0x70736620
    } 	BMDFieldDominance;

typedef /* [v1_enum] */ 
enum _BMDPixelFormat
    {	bmdFormat8BitYUV	= 0x32767579,
	bmdFormat10BitYUV	= 0x76323130,
	bmdFormat8BitARGB	= 32,
	bmdFormat8BitBGRA	= 0x42475241,
	bmdFormat10BitRGB	= 0x72323130
    } 	BMDPixelFormat;

/* [v1_enum] */ 
enum _BMDDisplayModeFlags
    {	bmdDisplayModeSupports3D	= ( 1 << 0 ) ,
	bmdDisplayModeColorspaceRec601	= ( 1 << 1 ) ,
	bmdDisplayModeColorspaceRec709	= ( 1 << 2 ) 
    } ;
typedef /* [v1_enum] */ 
enum _BMDVideoOutputFlags
    {	bmdVideoOutputFlagDefault	= 0,
	bmdVideoOutputVANC	= ( 1 << 0 ) ,
	bmdVideoOutputVITC	= ( 1 << 1 ) ,
	bmdVideoOutputRP188	= ( 1 << 2 ) ,
	bmdVideoOutputDualStream3D	= ( 1 << 4 ) 
    } 	BMDVideoOutputFlags;

/* [v1_enum] */ 
enum _BMDFrameFlags
    {	bmdFrameFlagDefault	= 0,
	bmdFrameFlagFlipVertical	= ( 1 << 0 ) ,
	bmdFrameHasNoInputSource	= ( 1 << 31 ) 
    } ;
/* [v1_enum] */ 
enum _BMDVideoInputFlags
    {	bmdVideoInputFlagDefault	= 0,
	bmdVideoInputEnableFormatDetection	= ( 1 << 0 ) ,
	bmdVideoInputDualStream3D	= ( 1 << 1 ) 
    } ;
/* [v1_enum] */ 
enum _BMDVideoInputFormatChangedEvents
    {	bmdVideoInputDisplayModeChanged	= ( 1 << 0 ) ,
	bmdVideoInputFieldDominanceChanged	= ( 1 << 1 ) ,
	bmdVideoInputColorspaceChanged	= ( 1 << 2 ) 
    } ;
/* [v1_enum] */ 
enum _BMDDetectedVideoInputFormatFlags
    {	bmdDetectedVideoInputYCbCr422	= ( 1 << 0 ) ,
	bmdDetectedVideoInputRGB444	= ( 1 << 1 ) 
    } ;
typedef /* [v1_enum] */ 
enum _BMDOutputFrameCompletionResult
    {	bmdOutputFrameCompleted	= 0,
	bmdOutputFrameDisplayedLate	= ( bmdOutputFrameCompleted + 1 ) ,
	bmdOutputFrameDropped	= ( bmdOutputFrameDisplayedLate + 1 ) ,
	bmdOutputFrameFlushed	= ( bmdOutputFrameDropped + 1 ) 
    } 	BMDOutputFrameCompletionResult;

typedef /* [v1_enum] */ 
enum _BMDReferenceStatus
    {	bmdReferenceNotSupportedByHardware	= ( 1 << 0 ) ,
	bmdReferenceLocked	= ( 1 << 1 ) 
    } 	BMDReferenceStatus;

typedef /* [v1_enum] */ 
enum _BMDAudioSampleRate
    {	bmdAudioSampleRate48kHz	= 48000
    } 	BMDAudioSampleRate;

typedef /* [v1_enum] */ 
enum _BMDAudioSampleType
    {	bmdAudioSampleType16bitInteger	= 16,
	bmdAudioSampleType32bitInteger	= 32
    } 	BMDAudioSampleType;

typedef /* [v1_enum] */ 
enum _BMDAudioOutputStreamType
    {	bmdAudioOutputStreamContinuous	= 0,
	bmdAudioOutputStreamContinuousDontResample	= ( bmdAudioOutputStreamContinuous + 1 ) ,
	bmdAudioOutputStreamTimestamped	= ( bmdAudioOutputStreamContinuousDontResample + 1 ) 
    } 	BMDAudioOutputStreamType;

typedef /* [v1_enum] */ 
enum _BMDDisplayModeSupport
    {	bmdDisplayModeNotSupported	= 0,
	bmdDisplayModeSupported	= ( bmdDisplayModeNotSupported + 1 ) ,
	bmdDisplayModeSupportedWithConversion	= ( bmdDisplayModeSupported + 1 ) 
    } 	BMDDisplayModeSupport;

typedef /* [v1_enum] */ 
enum _BMDTimecodeFormat
    {	bmdTimecodeRP188	= 0x72703138,
	bmdTimecodeRP188Field2	= 0x72703132,
	bmdTimecodeVITC	= 0x76697463,
	bmdTimecodeVITCField2	= 0x76697432,
	bmdTimecodeSerial	= 0x73657269
    } 	BMDTimecodeFormat;

/* [v1_enum] */ 
enum _BMDTimecodeFlags
    {	bmdTimecodeFlagDefault	= 0,
	bmdTimecodeIsDropFrame	= ( 1 << 0 ) 
    } ;
typedef /* [v1_enum] */ 
enum _BMDVideoConnection
    {	bmdVideoConnectionSDI	= ( 1 << 0 ) ,
	bmdVideoConnectionHDMI	= ( 1 << 1 ) ,
	bmdVideoConnectionOpticalSDI	= ( 1 << 2 ) ,
	bmdVideoConnectionComponent	= ( 1 << 3 ) ,
	bmdVideoConnectionComposite	= ( 1 << 4 ) ,
	bmdVideoConnectionSVideo	= ( 1 << 5 ) 
    } 	BMDVideoConnection;

/* [v1_enum] */ 
enum _BMDAnalogVideoFlags
    {	bmdAnalogVideoFlagCompositeSetup75	= ( 1 << 0 ) ,
	bmdAnalogVideoFlagComponentBetacamLevels	= ( 1 << 1 ) 
    } ;
typedef /* [v1_enum] */ 
enum _BMDAudioConnection
    {	bmdAudioConnectionEmbedded	= 0x656d6264,
	bmdAudioConnectionAESEBU	= 0x61657320,
	bmdAudioConnectionAnalog	= 0x616e6c67
    } 	BMDAudioConnection;

typedef /* [v1_enum] */ 
enum _BMDAudioOutputAnalogAESSwitch
    {	bmdAudioOutputSwitchAESEBU	= 0x61657320,
	bmdAudioOutputSwitchAnalog	= 0x616e6c67
    } 	BMDAudioOutputAnalogAESSwitch;

typedef /* [v1_enum] */ 
enum _BMDVideoOutputConversionMode
    {	bmdNoVideoOutputConversion	= 0x6e6f6e65,
	bmdVideoOutputLetterboxDownconversion	= 0x6c746278,
	bmdVideoOutputAnamorphicDownconversion	= 0x616d7068,
	bmdVideoOutputHD720toHD1080Conversion	= 0x37323063,
	bmdVideoOutputHardwareLetterboxDownconversion	= 0x48576c62,
	bmdVideoOutputHardwareAnamorphicDownconversion	= 0x4857616d,
	bmdVideoOutputHardwareCenterCutDownconversion	= 0x48576363,
	bmdVideoOutputHardware720p1080pCrossconversion	= 0x78636170,
	bmdVideoOutputHardwareAnamorphic720pUpconversion	= 0x75613770,
	bmdVideoOutputHardwareAnamorphic1080iUpconversion	= 0x75613169,
	bmdVideoOutputHardwareAnamorphic149To720pUpconversion	= 0x75343770,
	bmdVideoOutputHardwareAnamorphic149To1080iUpconversion	= 0x75343169,
	bmdVideoOutputHardwarePillarbox720pUpconversion	= 0x75703770,
	bmdVideoOutputHardwarePillarbox1080iUpconversion	= 0x75703169
    } 	BMDVideoOutputConversionMode;

typedef /* [v1_enum] */ 
enum _BMDVideoInputConversionMode
    {	bmdNoVideoInputConversion	= 0x6e6f6e65,
	bmdVideoInputLetterboxDownconversionFromHD1080	= 0x31306c62,
	bmdVideoInputAnamorphicDownconversionFromHD1080	= 0x3130616d,
	bmdVideoInputLetterboxDownconversionFromHD720	= 0x37326c62,
	bmdVideoInputAnamorphicDownconversionFromHD720	= 0x3732616d,
	bmdVideoInputLetterboxUpconversion	= 0x6c627570,
	bmdVideoInputAnamorphicUpconversion	= 0x616d7570
    } 	BMDVideoInputConversionMode;

typedef /* [v1_enum] */ 
enum _BMDVideo3DPackingFormat
    {	bmdVideo3DPackingSidebySideHalf	= 0x73627368,
	bmdVideo3DPackingLinebyLine	= 0x6c62796c,
	bmdVideo3DPackingTopAndBottom	= 0x7461626f,
	bmdVideo3DPackingFramePacking	= 0x6672706b,
	bmdVideo3DPackingLeftOnly	= 0x6c656674,
	bmdVideo3DPackingRightOnly	= 0x72696768
    } 	BMDVideo3DPackingFormat;

typedef /* [v1_enum] */ 
enum _BMDIdleVideoOutputOperation
    {	bmdIdleVideoOutputBlack	= 0x626c6163,
	bmdIdleVideoOutputLastFrame	= 0x6c616661
    } 	BMDIdleVideoOutputOperation;

typedef /* [v1_enum] */ 
enum _BMDDeckLinkConfigurationID
    {	bmdDeckLinkConfigSwapSerialRxTx	= 0x73737274,
	bmdDeckLinkConfigUse1080pNotPsF	= 0x6670726f,
	bmdDeckLinkConfigHDMI3DPackingFormat	= 0x33647066,
	bmdDeckLinkConfigBypass	= 0x62797073,
	bmdDeckLinkConfigAnalogAudioConsumerLevels	= 0x6161636c,
	bmdDeckLinkConfigFieldFlickerRemoval	= 0x66646672,
	bmdDeckLinkConfigHD1080p24ToHD1080i5994Conversion	= 0x746f3539,
	bmdDeckLinkConfig444SDIVideoOutput	= 0x3434346f,
	bmdDeckLinkConfig3GBpsVideoOutput	= 0x33676273,
	bmdDeckLinkConfigBlackVideoOutputDuringCapture	= 0x62766f63,
	bmdDeckLinkConfigLowLatencyVideoOutput	= 0x6c6c766f,
	bmdDeckLinkConfigVideoOutputConnection	= 0x766f636e,
	bmdDeckLinkConfigVideoOutputConversionMode	= 0x766f636d,
	bmdDeckLinkConfigAnalogVideoOutputFlags	= 0x61766f66,
	bmdDeckLinkConfigReferenceInputTimingOffset	= 0x676c6f74,
	bmdDeckLinkConfigVideoOutputIdleOperation	= 0x766f696f,
	bmdDeckLinkConfigVideoOutputComponentLumaGain	= 0x6f636c67,
	bmdDeckLinkConfigVideoOutputComponentChromaBlueGain	= 0x6f636362,
	bmdDeckLinkConfigVideoOutputComponentChromaRedGain	= 0x6f636372,
	bmdDeckLinkConfigVideoOutputCompositeLumaGain	= 0x6f696c67,
	bmdDeckLinkConfigVideoOutputCompositeChromaGain	= 0x6f696367,
	bmdDeckLinkConfigVideoOutputSVideoLumaGain	= 0x6f736c67,
	bmdDeckLinkConfigVideoOutputSVideoChromaGain	= 0x6f736367,
	bmdDeckLinkConfigVideoInputConnection	= 0x7669636e,
	bmdDeckLinkConfigAnalogVideoInputFlags	= 0x61766966,
	bmdDeckLinkConfigVideoInputConversionMode	= 0x7669636d,
	bmdDeckLinkConfig32PulldownSequenceInitialTimecodeFrame	= 0x70646966,
	bmdDeckLinkConfigVANCSourceLine1Mapping	= 0x76736c31,
	bmdDeckLinkConfigVANCSourceLine2Mapping	= 0x76736c32,
	bmdDeckLinkConfigVANCSourceLine3Mapping	= 0x76736c33,
	bmdDeckLinkConfigVideoInputComponentLumaGain	= 0x69636c67,
	bmdDeckLinkConfigVideoInputComponentChromaBlueGain	= 0x69636362,
	bmdDeckLinkConfigVideoInputComponentChromaRedGain	= 0x69636372,
	bmdDeckLinkConfigVideoInputCompositeLumaGain	= 0x69696c67,
	bmdDeckLinkConfigVideoInputCompositeChromaGain	= 0x69696367,
	bmdDeckLinkConfigVideoInputSVideoLumaGain	= 0x69736c67,
	bmdDeckLinkConfigVideoInputSVideoChromaGain	= 0x69736367,
	bmdDeckLinkConfigAudioInputConnection	= 0x6169636e,
	bmdDeckLinkConfigAnalogAudioInputScaleChannel1	= 0x61697331,
	bmdDeckLinkConfigAnalogAudioInputScaleChannel2	= 0x61697332,
	bmdDeckLinkConfigAnalogAudioInputScaleChannel3	= 0x61697333,
	bmdDeckLinkConfigAnalogAudioInputScaleChannel4	= 0x61697334,
	bmdDeckLinkConfigDigitalAudioInputScale	= 0x64616973,
	bmdDeckLinkConfigAudioOutputAESAnalogSwitch	= 0x616f6161,
	bmdDeckLinkConfigAnalogAudioOutputScaleChannel1	= 0x616f7331,
	bmdDeckLinkConfigAnalogAudioOutputScaleChannel2	= 0x616f7332,
	bmdDeckLinkConfigAnalogAudioOutputScaleChannel3	= 0x616f7333,
	bmdDeckLinkConfigAnalogAudioOutputScaleChannel4	= 0x616f7334,
	bmdDeckLinkConfigDigitalAudioOutputScale	= 0x64616f73
    } 	BMDDeckLinkConfigurationID;

typedef /* [v1_enum] */ 
enum _BMDDeckLinkAttributeID
    {	BMDDeckLinkSupportsInternalKeying	= 0x6b657969,
	BMDDeckLinkSupportsExternalKeying	= 0x6b657965,
	BMDDeckLinkSupportsHDKeying	= 0x6b657968,
	BMDDeckLinkSupportsInputFormatDetection	= 0x696e6664,
	BMDDeckLinkHasReferenceInput	= 0x6872696e,
	BMDDeckLinkHasSerialPort	= 0x68737074,
	BMDDeckLinkHasAnalogVideoOutputGain	= 0x61766f67,
	BMDDeckLinkCanOnlyAdjustOverallVideoOutputGain	= 0x6f766f67,
	BMDDeckLinkHasVideoInputAntiAliasingFilter	= 0x6161666c,
	BMDDeckLinkHasBypass	= 0x62797073,
	BMDDeckLinkMaximumAudioChannels	= 0x6d616368,
	BMDDeckLinkNumberOfSubDevices	= 0x6e736264,
	BMDDeckLinkSubDeviceIndex	= 0x73756269,
	BMDDeckLinkVideoOutputConnections	= 0x766f636e,
	BMDDeckLinkVideoInputConnections	= 0x7669636e,
	BMDDeckLinkVideoInputGainMinimum	= 0x7669676d,
	BMDDeckLinkVideoInputGainMaximum	= 0x76696778,
	BMDDeckLinkVideoOutputGainMinimum	= 0x766f676d,
	BMDDeckLinkVideoOutputGainMaximum	= 0x766f6778,
	BMDDeckLinkSerialPortDeviceName	= 0x736c706e
    } 	BMDDeckLinkAttributeID;

typedef /* [v1_enum] */ 
enum _BMDDeckLinkAPIInformationID
    {	BMDDeckLinkAPIVersion	= 0x76657273
    } 	BMDDeckLinkAPIInformationID;

typedef /* [v1_enum] */ 
enum _BMDDeckControlMode
    {	bmdDeckControlNotOpened	= 0x6e746f70,
	bmdDeckControlVTRControlMode	= 0x76747263,
	bmdDeckControlExportMode	= 0x6578706d,
	bmdDeckControlCaptureMode	= 0x6361706d
    } 	BMDDeckControlMode;

typedef /* [v1_enum] */ 
enum _BMDDeckControlEvent
    {	bmdDeckControlAbortedEvent	= 0x61627465,
	bmdDeckControlPrepareForExportEvent	= 0x70666565,
	bmdDeckControlExportCompleteEvent	= 0x65786365,
	bmdDeckControlPrepareForCaptureEvent	= 0x70666365,
	bmdDeckControlCaptureCompleteEvent	= 0x63636576
    } 	BMDDeckControlEvent;

typedef /* [v1_enum] */ 
enum _BMDDeckControlVTRControlState
    {	bmdDeckControlNotInVTRControlMode	= 0x6e76636d,
	bmdDeckControlVTRControlPlaying	= 0x76747270,
	bmdDeckControlVTRControlRecording	= 0x76747272,
	bmdDeckControlVTRControlStill	= 0x76747261,
	bmdDeckControlVTRControlSeeking	= 0x76747273,
	bmdDeckControlVTRControlStopped	= 0x7674726f
    } 	BMDDeckControlVTRControlState;

/* [v1_enum] */ 
enum _BMDDeckControlStatusFlags
    {	bmdDeckControlStatusDeckConnected	= ( 1 << 0 ) ,
	bmdDeckControlStatusRemoteMode	= ( 1 << 1 ) ,
	bmdDeckControlStatusRecordInhibited	= ( 1 << 2 ) ,
	bmdDeckControlStatusCassetteOut	= ( 1 << 3 ) 
    } ;
/* [v1_enum] */ 
enum _BMDDeckControlExportModeOpsFlags
    {	bmdDeckControlExportModeInsertVideo	= ( 1 << 0 ) ,
	bmdDeckControlExportModeInsertAudio1	= ( 1 << 1 ) ,
	bmdDeckControlExportModeInsertAudio2	= ( 1 << 2 ) ,
	bmdDeckControlExportModeInsertAudio3	= ( 1 << 3 ) ,
	bmdDeckControlExportModeInsertAudio4	= ( 1 << 4 ) ,
	bmdDeckControlExportModeInsertAudio5	= ( 1 << 5 ) ,
	bmdDeckControlExportModeInsertAudio6	= ( 1 << 6 ) ,
	bmdDeckControlExportModeInsertAudio7	= ( 1 << 7 ) ,
	bmdDeckControlExportModeInsertAudio8	= ( 1 << 8 ) ,
	bmdDeckControlExportModeInsertAudio9	= ( 1 << 9 ) ,
	bmdDeckControlExportModeInsertAudio10	= ( 1 << 10 ) ,
	bmdDeckControlExportModeInsertAudio11	= ( 1 << 11 ) ,
	bmdDeckControlExportModeInsertAudio12	= ( 1 << 12 ) ,
	bmdDeckControlExportModeInsertTimeCode	= ( 1 << 13 ) ,
	bmdDeckControlExportModeInsertAssemble	= ( 1 << 14 ) ,
	bmdDeckControlExportModeInsertPreview	= ( 1 << 15 ) ,
	bmdDeckControlUseManualExport	= ( 1 << 16 ) 
    } ;
typedef /* [v1_enum] */ 
enum _BMDDeckControlError
    {	bmdDeckControlNoError	= 0x6e6f6572,
	bmdDeckControlModeError	= 0x6d6f6572,
	bmdDeckControlMissedInPointError	= 0x6d696572,
	bmdDeckControlDeckTimeoutError	= 0x64746572,
	bmdDeckControlCommandFailedError	= 0x63666572,
	bmdDeckControlDeviceAlreadyOpenedError	= 0x64616c6f,
	bmdDeckControlFailedToOpenDeviceError	= 0x66646572,
	bmdDeckControlInLocalModeError	= 0x6c6d6572,
	bmdDeckControlEndOfTapeError	= 0x65746572,
	bmdDeckControlUserAbortError	= 0x75616572,
	bmdDeckControlNoTapeInDeckError	= 0x6e746572,
	bmdDeckControlNoVideoFromCardError	= 0x6e766663,
	bmdDeckControlNoCommunicationError	= 0x6e636f6d,
	bmdDeckControlBufferTooSmallError	= 0x6274736d,
	bmdDeckControlBadChecksumError	= 0x63686b73,
	bmdDeckControlUnknownError	= 0x756e6572
    } 	BMDDeckControlError;

typedef /* [v1_enum] */ 
enum _BMD3DPreviewFormat
    {	bmd3DPreviewFormatDefault	= 0x64656661,
	bmd3DPreviewFormatLeftOnly	= 0x6c656674,
	bmd3DPreviewFormatRightOnly	= 0x72696768,
	bmd3DPreviewFormatSideBySide	= 0x73696465,
	bmd3DPreviewFormatTopBottom	= 0x746f7062
    } 	BMD3DPreviewFormat;



























typedef /* [v1_enum] */ 
enum _BMDVideoConnection_v7_6
    {	bmdVideoConnectionSDI_v7_6	= 0x73646920,
	bmdVideoConnectionHDMI_v7_6	= 0x68646d69,
	bmdVideoConnectionOpticalSDI_v7_6	= 0x6f707469,
	bmdVideoConnectionComponent_v7_6	= 0x63706e74,
	bmdVideoConnectionComposite_v7_6	= 0x636d7374,
	bmdVideoConnectionSVideo_v7_6	= 0x73766964
    } 	BMDVideoConnection_v7_6;























EXTERN_C const IID LIBID_DeckLinkAPI;

#ifndef __IDeckLinkVideoOutputCallback_INTERFACE_DEFINED__
#define __IDeckLinkVideoOutputCallback_INTERFACE_DEFINED__

/* interface IDeckLinkVideoOutputCallback */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoOutputCallback;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("20AA5225-1958-47CB-820B-80A8D521A6EE")
    IDeckLinkVideoOutputCallback : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE ScheduledFrameCompleted( 
            /* [in] */ IDeckLinkVideoFrame *completedFrame,
            /* [in] */ BMDOutputFrameCompletionResult result) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ScheduledPlaybackHasStopped( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoOutputCallbackVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoOutputCallback * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoOutputCallback * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoOutputCallback * This);
        
        HRESULT ( STDMETHODCALLTYPE *ScheduledFrameCompleted )( 
            IDeckLinkVideoOutputCallback * This,
            /* [in] */ IDeckLinkVideoFrame *completedFrame,
            /* [in] */ BMDOutputFrameCompletionResult result);
        
        HRESULT ( STDMETHODCALLTYPE *ScheduledPlaybackHasStopped )( 
            IDeckLinkVideoOutputCallback * This);
        
        END_INTERFACE
    } IDeckLinkVideoOutputCallbackVtbl;

    interface IDeckLinkVideoOutputCallback
    {
        CONST_VTBL struct IDeckLinkVideoOutputCallbackVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoOutputCallback_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoOutputCallback_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoOutputCallback_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoOutputCallback_ScheduledFrameCompleted(This,completedFrame,result)	\
    ( (This)->lpVtbl -> ScheduledFrameCompleted(This,completedFrame,result) ) 

#define IDeckLinkVideoOutputCallback_ScheduledPlaybackHasStopped(This)	\
    ( (This)->lpVtbl -> ScheduledPlaybackHasStopped(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoOutputCallback_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkInputCallback_INTERFACE_DEFINED__
#define __IDeckLinkInputCallback_INTERFACE_DEFINED__

/* interface IDeckLinkInputCallback */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkInputCallback;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("DD04E5EC-7415-42AB-AE4A-E80C4DFC044A")
    IDeckLinkInputCallback : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged( 
            /* [in] */ BMDVideoInputFormatChangedEvents notificationEvents,
            /* [in] */ IDeckLinkDisplayMode *newDisplayMode,
            /* [in] */ BMDDetectedVideoInputFormatFlags detectedSignalFlags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived( 
            /* [in] */ IDeckLinkVideoInputFrame *videoFrame,
            /* [in] */ IDeckLinkAudioInputPacket *audioPacket) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkInputCallbackVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkInputCallback * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkInputCallback * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkInputCallback * This);
        
        HRESULT ( STDMETHODCALLTYPE *VideoInputFormatChanged )( 
            IDeckLinkInputCallback * This,
            /* [in] */ BMDVideoInputFormatChangedEvents notificationEvents,
            /* [in] */ IDeckLinkDisplayMode *newDisplayMode,
            /* [in] */ BMDDetectedVideoInputFormatFlags detectedSignalFlags);
        
        HRESULT ( STDMETHODCALLTYPE *VideoInputFrameArrived )( 
            IDeckLinkInputCallback * This,
            /* [in] */ IDeckLinkVideoInputFrame *videoFrame,
            /* [in] */ IDeckLinkAudioInputPacket *audioPacket);
        
        END_INTERFACE
    } IDeckLinkInputCallbackVtbl;

    interface IDeckLinkInputCallback
    {
        CONST_VTBL struct IDeckLinkInputCallbackVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkInputCallback_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkInputCallback_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkInputCallback_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkInputCallback_VideoInputFormatChanged(This,notificationEvents,newDisplayMode,detectedSignalFlags)	\
    ( (This)->lpVtbl -> VideoInputFormatChanged(This,notificationEvents,newDisplayMode,detectedSignalFlags) ) 

#define IDeckLinkInputCallback_VideoInputFrameArrived(This,videoFrame,audioPacket)	\
    ( (This)->lpVtbl -> VideoInputFrameArrived(This,videoFrame,audioPacket) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkInputCallback_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkMemoryAllocator_INTERFACE_DEFINED__
#define __IDeckLinkMemoryAllocator_INTERFACE_DEFINED__

/* interface IDeckLinkMemoryAllocator */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkMemoryAllocator;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("B36EB6E7-9D29-4AA8-92EF-843B87A289E8")
    IDeckLinkMemoryAllocator : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE AllocateBuffer( 
            /* [in] */ unsigned long bufferSize,
            /* [out] */ void **allocatedBuffer) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ReleaseBuffer( 
            /* [in] */ void *buffer) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Commit( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Decommit( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkMemoryAllocatorVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkMemoryAllocator * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkMemoryAllocator * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkMemoryAllocator * This);
        
        HRESULT ( STDMETHODCALLTYPE *AllocateBuffer )( 
            IDeckLinkMemoryAllocator * This,
            /* [in] */ unsigned long bufferSize,
            /* [out] */ void **allocatedBuffer);
        
        HRESULT ( STDMETHODCALLTYPE *ReleaseBuffer )( 
            IDeckLinkMemoryAllocator * This,
            /* [in] */ void *buffer);
        
        HRESULT ( STDMETHODCALLTYPE *Commit )( 
            IDeckLinkMemoryAllocator * This);
        
        HRESULT ( STDMETHODCALLTYPE *Decommit )( 
            IDeckLinkMemoryAllocator * This);
        
        END_INTERFACE
    } IDeckLinkMemoryAllocatorVtbl;

    interface IDeckLinkMemoryAllocator
    {
        CONST_VTBL struct IDeckLinkMemoryAllocatorVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkMemoryAllocator_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkMemoryAllocator_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkMemoryAllocator_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkMemoryAllocator_AllocateBuffer(This,bufferSize,allocatedBuffer)	\
    ( (This)->lpVtbl -> AllocateBuffer(This,bufferSize,allocatedBuffer) ) 

#define IDeckLinkMemoryAllocator_ReleaseBuffer(This,buffer)	\
    ( (This)->lpVtbl -> ReleaseBuffer(This,buffer) ) 

#define IDeckLinkMemoryAllocator_Commit(This)	\
    ( (This)->lpVtbl -> Commit(This) ) 

#define IDeckLinkMemoryAllocator_Decommit(This)	\
    ( (This)->lpVtbl -> Decommit(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkMemoryAllocator_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkAudioOutputCallback_INTERFACE_DEFINED__
#define __IDeckLinkAudioOutputCallback_INTERFACE_DEFINED__

/* interface IDeckLinkAudioOutputCallback */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkAudioOutputCallback;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("403C681B-7F46-4A12-B993-2BB127084EE6")
    IDeckLinkAudioOutputCallback : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE RenderAudioSamples( 
            /* [in] */ BOOL preroll) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkAudioOutputCallbackVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkAudioOutputCallback * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkAudioOutputCallback * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkAudioOutputCallback * This);
        
        HRESULT ( STDMETHODCALLTYPE *RenderAudioSamples )( 
            IDeckLinkAudioOutputCallback * This,
            /* [in] */ BOOL preroll);
        
        END_INTERFACE
    } IDeckLinkAudioOutputCallbackVtbl;

    interface IDeckLinkAudioOutputCallback
    {
        CONST_VTBL struct IDeckLinkAudioOutputCallbackVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkAudioOutputCallback_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkAudioOutputCallback_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkAudioOutputCallback_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkAudioOutputCallback_RenderAudioSamples(This,preroll)	\
    ( (This)->lpVtbl -> RenderAudioSamples(This,preroll) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkAudioOutputCallback_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkIterator_INTERFACE_DEFINED__
#define __IDeckLinkIterator_INTERFACE_DEFINED__

/* interface IDeckLinkIterator */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkIterator;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("74E936FC-CC28-4A67-81A0-1E94E52D4E69")
    IDeckLinkIterator : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE Next( 
            /* [out] */ IDeckLink **deckLinkInstance) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkIteratorVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkIterator * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkIterator * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkIterator * This);
        
        HRESULT ( STDMETHODCALLTYPE *Next )( 
            IDeckLinkIterator * This,
            /* [out] */ IDeckLink **deckLinkInstance);
        
        END_INTERFACE
    } IDeckLinkIteratorVtbl;

    interface IDeckLinkIterator
    {
        CONST_VTBL struct IDeckLinkIteratorVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkIterator_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkIterator_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkIterator_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkIterator_Next(This,deckLinkInstance)	\
    ( (This)->lpVtbl -> Next(This,deckLinkInstance) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkIterator_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkAPIInformation_INTERFACE_DEFINED__
#define __IDeckLinkAPIInformation_INTERFACE_DEFINED__

/* interface IDeckLinkAPIInformation */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkAPIInformation;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("7BEA3C68-730D-4322-AF34-8A7152B532A4")
    IDeckLinkAPIInformation : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetFlag( 
            /* [in] */ BMDDeckLinkAPIInformationID cfgID,
            /* [out] */ BOOL *value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetInt( 
            /* [in] */ BMDDeckLinkAPIInformationID cfgID,
            /* [out] */ LONGLONG *value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetFloat( 
            /* [in] */ BMDDeckLinkAPIInformationID cfgID,
            /* [out] */ double *value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetString( 
            /* [in] */ BMDDeckLinkAPIInformationID cfgID,
            /* [out] */ BSTR *value) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkAPIInformationVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkAPIInformation * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkAPIInformation * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkAPIInformation * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetFlag )( 
            IDeckLinkAPIInformation * This,
            /* [in] */ BMDDeckLinkAPIInformationID cfgID,
            /* [out] */ BOOL *value);
        
        HRESULT ( STDMETHODCALLTYPE *GetInt )( 
            IDeckLinkAPIInformation * This,
            /* [in] */ BMDDeckLinkAPIInformationID cfgID,
            /* [out] */ LONGLONG *value);
        
        HRESULT ( STDMETHODCALLTYPE *GetFloat )( 
            IDeckLinkAPIInformation * This,
            /* [in] */ BMDDeckLinkAPIInformationID cfgID,
            /* [out] */ double *value);
        
        HRESULT ( STDMETHODCALLTYPE *GetString )( 
            IDeckLinkAPIInformation * This,
            /* [in] */ BMDDeckLinkAPIInformationID cfgID,
            /* [out] */ BSTR *value);
        
        END_INTERFACE
    } IDeckLinkAPIInformationVtbl;

    interface IDeckLinkAPIInformation
    {
        CONST_VTBL struct IDeckLinkAPIInformationVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkAPIInformation_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkAPIInformation_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkAPIInformation_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkAPIInformation_GetFlag(This,cfgID,value)	\
    ( (This)->lpVtbl -> GetFlag(This,cfgID,value) ) 

#define IDeckLinkAPIInformation_GetInt(This,cfgID,value)	\
    ( (This)->lpVtbl -> GetInt(This,cfgID,value) ) 

#define IDeckLinkAPIInformation_GetFloat(This,cfgID,value)	\
    ( (This)->lpVtbl -> GetFloat(This,cfgID,value) ) 

#define IDeckLinkAPIInformation_GetString(This,cfgID,value)	\
    ( (This)->lpVtbl -> GetString(This,cfgID,value) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkAPIInformation_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkDisplayModeIterator_INTERFACE_DEFINED__
#define __IDeckLinkDisplayModeIterator_INTERFACE_DEFINED__

/* interface IDeckLinkDisplayModeIterator */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkDisplayModeIterator;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("9C88499F-F601-4021-B80B-032E4EB41C35")
    IDeckLinkDisplayModeIterator : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE Next( 
            /* [out] */ IDeckLinkDisplayMode **deckLinkDisplayMode) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkDisplayModeIteratorVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkDisplayModeIterator * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkDisplayModeIterator * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkDisplayModeIterator * This);
        
        HRESULT ( STDMETHODCALLTYPE *Next )( 
            IDeckLinkDisplayModeIterator * This,
            /* [out] */ IDeckLinkDisplayMode **deckLinkDisplayMode);
        
        END_INTERFACE
    } IDeckLinkDisplayModeIteratorVtbl;

    interface IDeckLinkDisplayModeIterator
    {
        CONST_VTBL struct IDeckLinkDisplayModeIteratorVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkDisplayModeIterator_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkDisplayModeIterator_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkDisplayModeIterator_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkDisplayModeIterator_Next(This,deckLinkDisplayMode)	\
    ( (This)->lpVtbl -> Next(This,deckLinkDisplayMode) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkDisplayModeIterator_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkDisplayMode_INTERFACE_DEFINED__
#define __IDeckLinkDisplayMode_INTERFACE_DEFINED__

/* interface IDeckLinkDisplayMode */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkDisplayMode;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("3EB2C1AB-0A3D-4523-A3AD-F40D7FB14E78")
    IDeckLinkDisplayMode : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetName( 
            /* [out] */ BSTR *name) = 0;
        
        virtual BMDDisplayMode STDMETHODCALLTYPE GetDisplayMode( void) = 0;
        
        virtual long STDMETHODCALLTYPE GetWidth( void) = 0;
        
        virtual long STDMETHODCALLTYPE GetHeight( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetFrameRate( 
            /* [out] */ BMDTimeValue *frameDuration,
            /* [out] */ BMDTimeScale *timeScale) = 0;
        
        virtual BMDFieldDominance STDMETHODCALLTYPE GetFieldDominance( void) = 0;
        
        virtual BMDDisplayModeFlags STDMETHODCALLTYPE GetFlags( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkDisplayModeVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkDisplayMode * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkDisplayMode * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkDisplayMode * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetName )( 
            IDeckLinkDisplayMode * This,
            /* [out] */ BSTR *name);
        
        BMDDisplayMode ( STDMETHODCALLTYPE *GetDisplayMode )( 
            IDeckLinkDisplayMode * This);
        
        long ( STDMETHODCALLTYPE *GetWidth )( 
            IDeckLinkDisplayMode * This);
        
        long ( STDMETHODCALLTYPE *GetHeight )( 
            IDeckLinkDisplayMode * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetFrameRate )( 
            IDeckLinkDisplayMode * This,
            /* [out] */ BMDTimeValue *frameDuration,
            /* [out] */ BMDTimeScale *timeScale);
        
        BMDFieldDominance ( STDMETHODCALLTYPE *GetFieldDominance )( 
            IDeckLinkDisplayMode * This);
        
        BMDDisplayModeFlags ( STDMETHODCALLTYPE *GetFlags )( 
            IDeckLinkDisplayMode * This);
        
        END_INTERFACE
    } IDeckLinkDisplayModeVtbl;

    interface IDeckLinkDisplayMode
    {
        CONST_VTBL struct IDeckLinkDisplayModeVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkDisplayMode_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkDisplayMode_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkDisplayMode_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkDisplayMode_GetName(This,name)	\
    ( (This)->lpVtbl -> GetName(This,name) ) 

#define IDeckLinkDisplayMode_GetDisplayMode(This)	\
    ( (This)->lpVtbl -> GetDisplayMode(This) ) 

#define IDeckLinkDisplayMode_GetWidth(This)	\
    ( (This)->lpVtbl -> GetWidth(This) ) 

#define IDeckLinkDisplayMode_GetHeight(This)	\
    ( (This)->lpVtbl -> GetHeight(This) ) 

#define IDeckLinkDisplayMode_GetFrameRate(This,frameDuration,timeScale)	\
    ( (This)->lpVtbl -> GetFrameRate(This,frameDuration,timeScale) ) 

#define IDeckLinkDisplayMode_GetFieldDominance(This)	\
    ( (This)->lpVtbl -> GetFieldDominance(This) ) 

#define IDeckLinkDisplayMode_GetFlags(This)	\
    ( (This)->lpVtbl -> GetFlags(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkDisplayMode_INTERFACE_DEFINED__ */


#ifndef __IDeckLink_INTERFACE_DEFINED__
#define __IDeckLink_INTERFACE_DEFINED__

/* interface IDeckLink */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLink;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("62BFF75D-6569-4E55-8D4D-66AA03829ABC")
    IDeckLink : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetModelName( 
            /* [out] */ BSTR *modelName) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLink * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLink * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLink * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetModelName )( 
            IDeckLink * This,
            /* [out] */ BSTR *modelName);
        
        END_INTERFACE
    } IDeckLinkVtbl;

    interface IDeckLink
    {
        CONST_VTBL struct IDeckLinkVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLink_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLink_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLink_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLink_GetModelName(This,modelName)	\
    ( (This)->lpVtbl -> GetModelName(This,modelName) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLink_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkOutput_INTERFACE_DEFINED__
#define __IDeckLinkOutput_INTERFACE_DEFINED__

/* interface IDeckLinkOutput */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkOutput;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("A3EF0963-0862-44ED-92A9-EE89ABF431C7")
    IDeckLinkOutput : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE DoesSupportVideoMode( 
            /* [in] */ BMDDisplayMode displayMode,
            /* [in] */ BMDPixelFormat pixelFormat,
            /* [in] */ BMDVideoOutputFlags flags,
            /* [out] */ BMDDisplayModeSupport *result,
            /* [out] */ IDeckLinkDisplayMode **resultDisplayMode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetDisplayModeIterator( 
            /* [out] */ IDeckLinkDisplayModeIterator **iterator) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetScreenPreviewCallback( 
            /* [in] */ IDeckLinkScreenPreviewCallback *previewCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableVideoOutput( 
            /* [in] */ BMDDisplayMode displayMode,
            /* [in] */ BMDVideoOutputFlags flags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableVideoOutput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetVideoOutputFrameMemoryAllocator( 
            /* [in] */ IDeckLinkMemoryAllocator *theAllocator) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE CreateVideoFrame( 
            /* [in] */ long width,
            /* [in] */ long height,
            /* [in] */ long rowBytes,
            /* [in] */ BMDPixelFormat pixelFormat,
            /* [in] */ BMDFrameFlags flags,
            /* [out] */ IDeckLinkMutableVideoFrame **outFrame) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE CreateAncillaryData( 
            /* [in] */ BMDPixelFormat pixelFormat,
            /* [out] */ IDeckLinkVideoFrameAncillary **outBuffer) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisplayVideoFrameSync( 
            /* [in] */ IDeckLinkVideoFrame *theFrame) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ScheduleVideoFrame( 
            /* [in] */ IDeckLinkVideoFrame *theFrame,
            /* [in] */ BMDTimeValue displayTime,
            /* [in] */ BMDTimeValue displayDuration,
            /* [in] */ BMDTimeScale timeScale) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetScheduledFrameCompletionCallback( 
            /* [in] */ IDeckLinkVideoOutputCallback *theCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBufferedVideoFrameCount( 
            /* [out] */ unsigned long *bufferedFrameCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableAudioOutput( 
            /* [in] */ BMDAudioSampleRate sampleRate,
            /* [in] */ BMDAudioSampleType sampleType,
            /* [in] */ unsigned long channelCount,
            /* [in] */ BMDAudioOutputStreamType streamType) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableAudioOutput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE WriteAudioSamplesSync( 
            /* [in] */ void *buffer,
            /* [in] */ unsigned long sampleFrameCount,
            /* [out] */ unsigned long *sampleFramesWritten) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE BeginAudioPreroll( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EndAudioPreroll( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ScheduleAudioSamples( 
            /* [in] */ void *buffer,
            /* [in] */ unsigned long sampleFrameCount,
            /* [in] */ BMDTimeValue streamTime,
            /* [in] */ BMDTimeScale timeScale,
            /* [out] */ unsigned long *sampleFramesWritten) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBufferedAudioSampleFrameCount( 
            /* [out] */ unsigned long *bufferedSampleFrameCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE FlushBufferedAudioSamples( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetAudioCallback( 
            /* [in] */ IDeckLinkAudioOutputCallback *theCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StartScheduledPlayback( 
            /* [in] */ BMDTimeValue playbackStartTime,
            /* [in] */ BMDTimeScale timeScale,
            /* [in] */ double playbackSpeed) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StopScheduledPlayback( 
            /* [in] */ BMDTimeValue stopPlaybackAtTime,
            /* [out] */ BMDTimeValue *actualStopTime,
            /* [in] */ BMDTimeScale timeScale) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE IsScheduledPlaybackRunning( 
            /* [out] */ BOOL *active) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetScheduledStreamTime( 
            /* [in] */ BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *streamTime,
            /* [out] */ double *playbackSpeed) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetReferenceStatus( 
            /* [out] */ BMDReferenceStatus *referenceStatus) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetHardwareReferenceClock( 
            /* [in] */ BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *hardwareTime,
            /* [out] */ BMDTimeValue *timeInFrame,
            /* [out] */ BMDTimeValue *ticksPerFrame) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkOutputVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkOutput * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkOutput * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkOutput * This);
        
        HRESULT ( STDMETHODCALLTYPE *DoesSupportVideoMode )( 
            IDeckLinkOutput * This,
            /* [in] */ BMDDisplayMode displayMode,
            /* [in] */ BMDPixelFormat pixelFormat,
            /* [in] */ BMDVideoOutputFlags flags,
            /* [out] */ BMDDisplayModeSupport *result,
            /* [out] */ IDeckLinkDisplayMode **resultDisplayMode);
        
        HRESULT ( STDMETHODCALLTYPE *GetDisplayModeIterator )( 
            IDeckLinkOutput * This,
            /* [out] */ IDeckLinkDisplayModeIterator **iterator);
        
        HRESULT ( STDMETHODCALLTYPE *SetScreenPreviewCallback )( 
            IDeckLinkOutput * This,
            /* [in] */ IDeckLinkScreenPreviewCallback *previewCallback);
        
        HRESULT ( STDMETHODCALLTYPE *EnableVideoOutput )( 
            IDeckLinkOutput * This,
            /* [in] */ BMDDisplayMode displayMode,
            /* [in] */ BMDVideoOutputFlags flags);
        
        HRESULT ( STDMETHODCALLTYPE *DisableVideoOutput )( 
            IDeckLinkOutput * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetVideoOutputFrameMemoryAllocator )( 
            IDeckLinkOutput * This,
            /* [in] */ IDeckLinkMemoryAllocator *theAllocator);
        
        HRESULT ( STDMETHODCALLTYPE *CreateVideoFrame )( 
            IDeckLinkOutput * This,
            /* [in] */ long width,
            /* [in] */ long height,
            /* [in] */ long rowBytes,
            /* [in] */ BMDPixelFormat pixelFormat,
            /* [in] */ BMDFrameFlags flags,
            /* [out] */ IDeckLinkMutableVideoFrame **outFrame);
        
        HRESULT ( STDMETHODCALLTYPE *CreateAncillaryData )( 
            IDeckLinkOutput * This,
            /* [in] */ BMDPixelFormat pixelFormat,
            /* [out] */ IDeckLinkVideoFrameAncillary **outBuffer);
        
        HRESULT ( STDMETHODCALLTYPE *DisplayVideoFrameSync )( 
            IDeckLinkOutput * This,
            /* [in] */ IDeckLinkVideoFrame *theFrame);
        
        HRESULT ( STDMETHODCALLTYPE *ScheduleVideoFrame )( 
            IDeckLinkOutput * This,
            /* [in] */ IDeckLinkVideoFrame *theFrame,
            /* [in] */ BMDTimeValue displayTime,
            /* [in] */ BMDTimeValue displayDuration,
            /* [in] */ BMDTimeScale timeScale);
        
        HRESULT ( STDMETHODCALLTYPE *SetScheduledFrameCompletionCallback )( 
            IDeckLinkOutput * This,
            /* [in] */ IDeckLinkVideoOutputCallback *theCallback);
        
        HRESULT ( STDMETHODCALLTYPE *GetBufferedVideoFrameCount )( 
            IDeckLinkOutput * This,
            /* [out] */ unsigned long *bufferedFrameCount);
        
        HRESULT ( STDMETHODCALLTYPE *EnableAudioOutput )( 
            IDeckLinkOutput * This,
            /* [in] */ BMDAudioSampleRate sampleRate,
            /* [in] */ BMDAudioSampleType sampleType,
            /* [in] */ unsigned long channelCount,
            /* [in] */ BMDAudioOutputStreamType streamType);
        
        HRESULT ( STDMETHODCALLTYPE *DisableAudioOutput )( 
            IDeckLinkOutput * This);
        
        HRESULT ( STDMETHODCALLTYPE *WriteAudioSamplesSync )( 
            IDeckLinkOutput * This,
            /* [in] */ void *buffer,
            /* [in] */ unsigned long sampleFrameCount,
            /* [out] */ unsigned long *sampleFramesWritten);
        
        HRESULT ( STDMETHODCALLTYPE *BeginAudioPreroll )( 
            IDeckLinkOutput * This);
        
        HRESULT ( STDMETHODCALLTYPE *EndAudioPreroll )( 
            IDeckLinkOutput * This);
        
        HRESULT ( STDMETHODCALLTYPE *ScheduleAudioSamples )( 
            IDeckLinkOutput * This,
            /* [in] */ void *buffer,
            /* [in] */ unsigned long sampleFrameCount,
            /* [in] */ BMDTimeValue streamTime,
            /* [in] */ BMDTimeScale timeScale,
            /* [out] */ unsigned long *sampleFramesWritten);
        
        HRESULT ( STDMETHODCALLTYPE *GetBufferedAudioSampleFrameCount )( 
            IDeckLinkOutput * This,
            /* [out] */ unsigned long *bufferedSampleFrameCount);
        
        HRESULT ( STDMETHODCALLTYPE *FlushBufferedAudioSamples )( 
            IDeckLinkOutput * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetAudioCallback )( 
            IDeckLinkOutput * This,
            /* [in] */ IDeckLinkAudioOutputCallback *theCallback);
        
        HRESULT ( STDMETHODCALLTYPE *StartScheduledPlayback )( 
            IDeckLinkOutput * This,
            /* [in] */ BMDTimeValue playbackStartTime,
            /* [in] */ BMDTimeScale timeScale,
            /* [in] */ double playbackSpeed);
        
        HRESULT ( STDMETHODCALLTYPE *StopScheduledPlayback )( 
            IDeckLinkOutput * This,
            /* [in] */ BMDTimeValue stopPlaybackAtTime,
            /* [out] */ BMDTimeValue *actualStopTime,
            /* [in] */ BMDTimeScale timeScale);
        
        HRESULT ( STDMETHODCALLTYPE *IsScheduledPlaybackRunning )( 
            IDeckLinkOutput * This,
            /* [out] */ BOOL *active);
        
        HRESULT ( STDMETHODCALLTYPE *GetScheduledStreamTime )( 
            IDeckLinkOutput * This,
            /* [in] */ BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *streamTime,
            /* [out] */ double *playbackSpeed);
        
        HRESULT ( STDMETHODCALLTYPE *GetReferenceStatus )( 
            IDeckLinkOutput * This,
            /* [out] */ BMDReferenceStatus *referenceStatus);
        
        HRESULT ( STDMETHODCALLTYPE *GetHardwareReferenceClock )( 
            IDeckLinkOutput * This,
            /* [in] */ BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *hardwareTime,
            /* [out] */ BMDTimeValue *timeInFrame,
            /* [out] */ BMDTimeValue *ticksPerFrame);
        
        END_INTERFACE
    } IDeckLinkOutputVtbl;

    interface IDeckLinkOutput
    {
        CONST_VTBL struct IDeckLinkOutputVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkOutput_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkOutput_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkOutput_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkOutput_DoesSupportVideoMode(This,displayMode,pixelFormat,flags,result,resultDisplayMode)	\
    ( (This)->lpVtbl -> DoesSupportVideoMode(This,displayMode,pixelFormat,flags,result,resultDisplayMode) ) 

#define IDeckLinkOutput_GetDisplayModeIterator(This,iterator)	\
    ( (This)->lpVtbl -> GetDisplayModeIterator(This,iterator) ) 

#define IDeckLinkOutput_SetScreenPreviewCallback(This,previewCallback)	\
    ( (This)->lpVtbl -> SetScreenPreviewCallback(This,previewCallback) ) 

#define IDeckLinkOutput_EnableVideoOutput(This,displayMode,flags)	\
    ( (This)->lpVtbl -> EnableVideoOutput(This,displayMode,flags) ) 

#define IDeckLinkOutput_DisableVideoOutput(This)	\
    ( (This)->lpVtbl -> DisableVideoOutput(This) ) 

#define IDeckLinkOutput_SetVideoOutputFrameMemoryAllocator(This,theAllocator)	\
    ( (This)->lpVtbl -> SetVideoOutputFrameMemoryAllocator(This,theAllocator) ) 

#define IDeckLinkOutput_CreateVideoFrame(This,width,height,rowBytes,pixelFormat,flags,outFrame)	\
    ( (This)->lpVtbl -> CreateVideoFrame(This,width,height,rowBytes,pixelFormat,flags,outFrame) ) 

#define IDeckLinkOutput_CreateAncillaryData(This,pixelFormat,outBuffer)	\
    ( (This)->lpVtbl -> CreateAncillaryData(This,pixelFormat,outBuffer) ) 

#define IDeckLinkOutput_DisplayVideoFrameSync(This,theFrame)	\
    ( (This)->lpVtbl -> DisplayVideoFrameSync(This,theFrame) ) 

#define IDeckLinkOutput_ScheduleVideoFrame(This,theFrame,displayTime,displayDuration,timeScale)	\
    ( (This)->lpVtbl -> ScheduleVideoFrame(This,theFrame,displayTime,displayDuration,timeScale) ) 

#define IDeckLinkOutput_SetScheduledFrameCompletionCallback(This,theCallback)	\
    ( (This)->lpVtbl -> SetScheduledFrameCompletionCallback(This,theCallback) ) 

#define IDeckLinkOutput_GetBufferedVideoFrameCount(This,bufferedFrameCount)	\
    ( (This)->lpVtbl -> GetBufferedVideoFrameCount(This,bufferedFrameCount) ) 

#define IDeckLinkOutput_EnableAudioOutput(This,sampleRate,sampleType,channelCount,streamType)	\
    ( (This)->lpVtbl -> EnableAudioOutput(This,sampleRate,sampleType,channelCount,streamType) ) 

#define IDeckLinkOutput_DisableAudioOutput(This)	\
    ( (This)->lpVtbl -> DisableAudioOutput(This) ) 

#define IDeckLinkOutput_WriteAudioSamplesSync(This,buffer,sampleFrameCount,sampleFramesWritten)	\
    ( (This)->lpVtbl -> WriteAudioSamplesSync(This,buffer,sampleFrameCount,sampleFramesWritten) ) 

#define IDeckLinkOutput_BeginAudioPreroll(This)	\
    ( (This)->lpVtbl -> BeginAudioPreroll(This) ) 

#define IDeckLinkOutput_EndAudioPreroll(This)	\
    ( (This)->lpVtbl -> EndAudioPreroll(This) ) 

#define IDeckLinkOutput_ScheduleAudioSamples(This,buffer,sampleFrameCount,streamTime,timeScale,sampleFramesWritten)	\
    ( (This)->lpVtbl -> ScheduleAudioSamples(This,buffer,sampleFrameCount,streamTime,timeScale,sampleFramesWritten) ) 

#define IDeckLinkOutput_GetBufferedAudioSampleFrameCount(This,bufferedSampleFrameCount)	\
    ( (This)->lpVtbl -> GetBufferedAudioSampleFrameCount(This,bufferedSampleFrameCount) ) 

#define IDeckLinkOutput_FlushBufferedAudioSamples(This)	\
    ( (This)->lpVtbl -> FlushBufferedAudioSamples(This) ) 

#define IDeckLinkOutput_SetAudioCallback(This,theCallback)	\
    ( (This)->lpVtbl -> SetAudioCallback(This,theCallback) ) 

#define IDeckLinkOutput_StartScheduledPlayback(This,playbackStartTime,timeScale,playbackSpeed)	\
    ( (This)->lpVtbl -> StartScheduledPlayback(This,playbackStartTime,timeScale,playbackSpeed) ) 

#define IDeckLinkOutput_StopScheduledPlayback(This,stopPlaybackAtTime,actualStopTime,timeScale)	\
    ( (This)->lpVtbl -> StopScheduledPlayback(This,stopPlaybackAtTime,actualStopTime,timeScale) ) 

#define IDeckLinkOutput_IsScheduledPlaybackRunning(This,active)	\
    ( (This)->lpVtbl -> IsScheduledPlaybackRunning(This,active) ) 

#define IDeckLinkOutput_GetScheduledStreamTime(This,desiredTimeScale,streamTime,playbackSpeed)	\
    ( (This)->lpVtbl -> GetScheduledStreamTime(This,desiredTimeScale,streamTime,playbackSpeed) ) 

#define IDeckLinkOutput_GetReferenceStatus(This,referenceStatus)	\
    ( (This)->lpVtbl -> GetReferenceStatus(This,referenceStatus) ) 

#define IDeckLinkOutput_GetHardwareReferenceClock(This,desiredTimeScale,hardwareTime,timeInFrame,ticksPerFrame)	\
    ( (This)->lpVtbl -> GetHardwareReferenceClock(This,desiredTimeScale,hardwareTime,timeInFrame,ticksPerFrame) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkOutput_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkInput_INTERFACE_DEFINED__
#define __IDeckLinkInput_INTERFACE_DEFINED__

/* interface IDeckLinkInput */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkInput;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("6D40EF78-28B9-4E21-990D-95BB7750A04F")
    IDeckLinkInput : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE DoesSupportVideoMode( 
            /* [in] */ BMDDisplayMode displayMode,
            /* [in] */ BMDPixelFormat pixelFormat,
            /* [in] */ BMDVideoInputFlags flags,
            /* [out] */ BMDDisplayModeSupport *result,
            /* [out] */ IDeckLinkDisplayMode **resultDisplayMode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetDisplayModeIterator( 
            /* [out] */ IDeckLinkDisplayModeIterator **iterator) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetScreenPreviewCallback( 
            /* [in] */ IDeckLinkScreenPreviewCallback *previewCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableVideoInput( 
            /* [in] */ BMDDisplayMode displayMode,
            /* [in] */ BMDPixelFormat pixelFormat,
            /* [in] */ BMDVideoInputFlags flags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableVideoInput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetAvailableVideoFrameCount( 
            /* [out] */ unsigned long *availableFrameCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableAudioInput( 
            /* [in] */ BMDAudioSampleRate sampleRate,
            /* [in] */ BMDAudioSampleType sampleType,
            /* [in] */ unsigned long channelCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableAudioInput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetAvailableAudioSampleFrameCount( 
            /* [out] */ unsigned long *availableSampleFrameCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StartStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StopStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE PauseStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE FlushStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetCallback( 
            /* [in] */ IDeckLinkInputCallback *theCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetHardwareReferenceClock( 
            /* [in] */ BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *hardwareTime,
            /* [out] */ BMDTimeValue *timeInFrame,
            /* [out] */ BMDTimeValue *ticksPerFrame) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkInputVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkInput * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkInput * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkInput * This);
        
        HRESULT ( STDMETHODCALLTYPE *DoesSupportVideoMode )( 
            IDeckLinkInput * This,
            /* [in] */ BMDDisplayMode displayMode,
            /* [in] */ BMDPixelFormat pixelFormat,
            /* [in] */ BMDVideoInputFlags flags,
            /* [out] */ BMDDisplayModeSupport *result,
            /* [out] */ IDeckLinkDisplayMode **resultDisplayMode);
        
        HRESULT ( STDMETHODCALLTYPE *GetDisplayModeIterator )( 
            IDeckLinkInput * This,
            /* [out] */ IDeckLinkDisplayModeIterator **iterator);
        
        HRESULT ( STDMETHODCALLTYPE *SetScreenPreviewCallback )( 
            IDeckLinkInput * This,
            /* [in] */ IDeckLinkScreenPreviewCallback *previewCallback);
        
        HRESULT ( STDMETHODCALLTYPE *EnableVideoInput )( 
            IDeckLinkInput * This,
            /* [in] */ BMDDisplayMode displayMode,
            /* [in] */ BMDPixelFormat pixelFormat,
            /* [in] */ BMDVideoInputFlags flags);
        
        HRESULT ( STDMETHODCALLTYPE *DisableVideoInput )( 
            IDeckLinkInput * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetAvailableVideoFrameCount )( 
            IDeckLinkInput * This,
            /* [out] */ unsigned long *availableFrameCount);
        
        HRESULT ( STDMETHODCALLTYPE *EnableAudioInput )( 
            IDeckLinkInput * This,
            /* [in] */ BMDAudioSampleRate sampleRate,
            /* [in] */ BMDAudioSampleType sampleType,
            /* [in] */ unsigned long channelCount);
        
        HRESULT ( STDMETHODCALLTYPE *DisableAudioInput )( 
            IDeckLinkInput * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetAvailableAudioSampleFrameCount )( 
            IDeckLinkInput * This,
            /* [out] */ unsigned long *availableSampleFrameCount);
        
        HRESULT ( STDMETHODCALLTYPE *StartStreams )( 
            IDeckLinkInput * This);
        
        HRESULT ( STDMETHODCALLTYPE *StopStreams )( 
            IDeckLinkInput * This);
        
        HRESULT ( STDMETHODCALLTYPE *PauseStreams )( 
            IDeckLinkInput * This);
        
        HRESULT ( STDMETHODCALLTYPE *FlushStreams )( 
            IDeckLinkInput * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetCallback )( 
            IDeckLinkInput * This,
            /* [in] */ IDeckLinkInputCallback *theCallback);
        
        HRESULT ( STDMETHODCALLTYPE *GetHardwareReferenceClock )( 
            IDeckLinkInput * This,
            /* [in] */ BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *hardwareTime,
            /* [out] */ BMDTimeValue *timeInFrame,
            /* [out] */ BMDTimeValue *ticksPerFrame);
        
        END_INTERFACE
    } IDeckLinkInputVtbl;

    interface IDeckLinkInput
    {
        CONST_VTBL struct IDeckLinkInputVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkInput_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkInput_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkInput_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkInput_DoesSupportVideoMode(This,displayMode,pixelFormat,flags,result,resultDisplayMode)	\
    ( (This)->lpVtbl -> DoesSupportVideoMode(This,displayMode,pixelFormat,flags,result,resultDisplayMode) ) 

#define IDeckLinkInput_GetDisplayModeIterator(This,iterator)	\
    ( (This)->lpVtbl -> GetDisplayModeIterator(This,iterator) ) 

#define IDeckLinkInput_SetScreenPreviewCallback(This,previewCallback)	\
    ( (This)->lpVtbl -> SetScreenPreviewCallback(This,previewCallback) ) 

#define IDeckLinkInput_EnableVideoInput(This,displayMode,pixelFormat,flags)	\
    ( (This)->lpVtbl -> EnableVideoInput(This,displayMode,pixelFormat,flags) ) 

#define IDeckLinkInput_DisableVideoInput(This)	\
    ( (This)->lpVtbl -> DisableVideoInput(This) ) 

#define IDeckLinkInput_GetAvailableVideoFrameCount(This,availableFrameCount)	\
    ( (This)->lpVtbl -> GetAvailableVideoFrameCount(This,availableFrameCount) ) 

#define IDeckLinkInput_EnableAudioInput(This,sampleRate,sampleType,channelCount)	\
    ( (This)->lpVtbl -> EnableAudioInput(This,sampleRate,sampleType,channelCount) ) 

#define IDeckLinkInput_DisableAudioInput(This)	\
    ( (This)->lpVtbl -> DisableAudioInput(This) ) 

#define IDeckLinkInput_GetAvailableAudioSampleFrameCount(This,availableSampleFrameCount)	\
    ( (This)->lpVtbl -> GetAvailableAudioSampleFrameCount(This,availableSampleFrameCount) ) 

#define IDeckLinkInput_StartStreams(This)	\
    ( (This)->lpVtbl -> StartStreams(This) ) 

#define IDeckLinkInput_StopStreams(This)	\
    ( (This)->lpVtbl -> StopStreams(This) ) 

#define IDeckLinkInput_PauseStreams(This)	\
    ( (This)->lpVtbl -> PauseStreams(This) ) 

#define IDeckLinkInput_FlushStreams(This)	\
    ( (This)->lpVtbl -> FlushStreams(This) ) 

#define IDeckLinkInput_SetCallback(This,theCallback)	\
    ( (This)->lpVtbl -> SetCallback(This,theCallback) ) 

#define IDeckLinkInput_GetHardwareReferenceClock(This,desiredTimeScale,hardwareTime,timeInFrame,ticksPerFrame)	\
    ( (This)->lpVtbl -> GetHardwareReferenceClock(This,desiredTimeScale,hardwareTime,timeInFrame,ticksPerFrame) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkInput_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkTimecode_INTERFACE_DEFINED__
#define __IDeckLinkTimecode_INTERFACE_DEFINED__

/* interface IDeckLinkTimecode */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkTimecode;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("BC6CFBD3-8317-4325-AC1C-1216391E9340")
    IDeckLinkTimecode : public IUnknown
    {
    public:
        virtual BMDTimecodeBCD STDMETHODCALLTYPE GetBCD( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetComponents( 
            /* [out] */ unsigned char *hours,
            /* [out] */ unsigned char *minutes,
            /* [out] */ unsigned char *seconds,
            /* [out] */ unsigned char *frames) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetString( 
            /* [out] */ BSTR *timecode) = 0;
        
        virtual BMDTimecodeFlags STDMETHODCALLTYPE GetFlags( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetTimecodeUserBits( 
            /* [out] */ BMDTimecodeUserBits *userBits) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkTimecodeVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkTimecode * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkTimecode * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkTimecode * This);
        
        BMDTimecodeBCD ( STDMETHODCALLTYPE *GetBCD )( 
            IDeckLinkTimecode * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetComponents )( 
            IDeckLinkTimecode * This,
            /* [out] */ unsigned char *hours,
            /* [out] */ unsigned char *minutes,
            /* [out] */ unsigned char *seconds,
            /* [out] */ unsigned char *frames);
        
        HRESULT ( STDMETHODCALLTYPE *GetString )( 
            IDeckLinkTimecode * This,
            /* [out] */ BSTR *timecode);
        
        BMDTimecodeFlags ( STDMETHODCALLTYPE *GetFlags )( 
            IDeckLinkTimecode * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecodeUserBits )( 
            IDeckLinkTimecode * This,
            /* [out] */ BMDTimecodeUserBits *userBits);
        
        END_INTERFACE
    } IDeckLinkTimecodeVtbl;

    interface IDeckLinkTimecode
    {
        CONST_VTBL struct IDeckLinkTimecodeVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkTimecode_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkTimecode_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkTimecode_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkTimecode_GetBCD(This)	\
    ( (This)->lpVtbl -> GetBCD(This) ) 

#define IDeckLinkTimecode_GetComponents(This,hours,minutes,seconds,frames)	\
    ( (This)->lpVtbl -> GetComponents(This,hours,minutes,seconds,frames) ) 

#define IDeckLinkTimecode_GetString(This,timecode)	\
    ( (This)->lpVtbl -> GetString(This,timecode) ) 

#define IDeckLinkTimecode_GetFlags(This)	\
    ( (This)->lpVtbl -> GetFlags(This) ) 

#define IDeckLinkTimecode_GetTimecodeUserBits(This,userBits)	\
    ( (This)->lpVtbl -> GetTimecodeUserBits(This,userBits) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkTimecode_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkVideoFrame_INTERFACE_DEFINED__
#define __IDeckLinkVideoFrame_INTERFACE_DEFINED__

/* interface IDeckLinkVideoFrame */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoFrame;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("3F716FE0-F023-4111-BE5D-EF4414C05B17")
    IDeckLinkVideoFrame : public IUnknown
    {
    public:
        virtual long STDMETHODCALLTYPE GetWidth( void) = 0;
        
        virtual long STDMETHODCALLTYPE GetHeight( void) = 0;
        
        virtual long STDMETHODCALLTYPE GetRowBytes( void) = 0;
        
        virtual BMDPixelFormat STDMETHODCALLTYPE GetPixelFormat( void) = 0;
        
        virtual BMDFrameFlags STDMETHODCALLTYPE GetFlags( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBytes( 
            /* [out] */ void **buffer) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetTimecode( 
            /* [in] */ BMDTimecodeFormat format,
            /* [out] */ IDeckLinkTimecode **timecode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetAncillaryData( 
            /* [out] */ IDeckLinkVideoFrameAncillary **ancillary) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoFrameVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoFrame * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoFrame * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoFrame * This);
        
        long ( STDMETHODCALLTYPE *GetWidth )( 
            IDeckLinkVideoFrame * This);
        
        long ( STDMETHODCALLTYPE *GetHeight )( 
            IDeckLinkVideoFrame * This);
        
        long ( STDMETHODCALLTYPE *GetRowBytes )( 
            IDeckLinkVideoFrame * This);
        
        BMDPixelFormat ( STDMETHODCALLTYPE *GetPixelFormat )( 
            IDeckLinkVideoFrame * This);
        
        BMDFrameFlags ( STDMETHODCALLTYPE *GetFlags )( 
            IDeckLinkVideoFrame * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetBytes )( 
            IDeckLinkVideoFrame * This,
            /* [out] */ void **buffer);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecode )( 
            IDeckLinkVideoFrame * This,
            /* [in] */ BMDTimecodeFormat format,
            /* [out] */ IDeckLinkTimecode **timecode);
        
        HRESULT ( STDMETHODCALLTYPE *GetAncillaryData )( 
            IDeckLinkVideoFrame * This,
            /* [out] */ IDeckLinkVideoFrameAncillary **ancillary);
        
        END_INTERFACE
    } IDeckLinkVideoFrameVtbl;

    interface IDeckLinkVideoFrame
    {
        CONST_VTBL struct IDeckLinkVideoFrameVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoFrame_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoFrame_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoFrame_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoFrame_GetWidth(This)	\
    ( (This)->lpVtbl -> GetWidth(This) ) 

#define IDeckLinkVideoFrame_GetHeight(This)	\
    ( (This)->lpVtbl -> GetHeight(This) ) 

#define IDeckLinkVideoFrame_GetRowBytes(This)	\
    ( (This)->lpVtbl -> GetRowBytes(This) ) 

#define IDeckLinkVideoFrame_GetPixelFormat(This)	\
    ( (This)->lpVtbl -> GetPixelFormat(This) ) 

#define IDeckLinkVideoFrame_GetFlags(This)	\
    ( (This)->lpVtbl -> GetFlags(This) ) 

#define IDeckLinkVideoFrame_GetBytes(This,buffer)	\
    ( (This)->lpVtbl -> GetBytes(This,buffer) ) 

#define IDeckLinkVideoFrame_GetTimecode(This,format,timecode)	\
    ( (This)->lpVtbl -> GetTimecode(This,format,timecode) ) 

#define IDeckLinkVideoFrame_GetAncillaryData(This,ancillary)	\
    ( (This)->lpVtbl -> GetAncillaryData(This,ancillary) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoFrame_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkMutableVideoFrame_INTERFACE_DEFINED__
#define __IDeckLinkMutableVideoFrame_INTERFACE_DEFINED__

/* interface IDeckLinkMutableVideoFrame */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkMutableVideoFrame;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("69E2639F-40DA-4E19-B6F2-20ACE815C390")
    IDeckLinkMutableVideoFrame : public IDeckLinkVideoFrame
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE SetFlags( 
            /* [in] */ BMDFrameFlags newFlags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetTimecode( 
            /* [in] */ BMDTimecodeFormat format,
            /* [in] */ IDeckLinkTimecode *timecode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetTimecodeFromComponents( 
            /* [in] */ BMDTimecodeFormat format,
            /* [in] */ unsigned char hours,
            /* [in] */ unsigned char minutes,
            /* [in] */ unsigned char seconds,
            /* [in] */ unsigned char frames,
            /* [in] */ BMDTimecodeFlags flags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetAncillaryData( 
            /* [in] */ IDeckLinkVideoFrameAncillary *ancillary) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetTimecodeUserBits( 
            /* [in] */ BMDTimecodeFormat format,
            /* [in] */ BMDTimecodeUserBits userBits) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkMutableVideoFrameVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkMutableVideoFrame * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkMutableVideoFrame * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkMutableVideoFrame * This);
        
        long ( STDMETHODCALLTYPE *GetWidth )( 
            IDeckLinkMutableVideoFrame * This);
        
        long ( STDMETHODCALLTYPE *GetHeight )( 
            IDeckLinkMutableVideoFrame * This);
        
        long ( STDMETHODCALLTYPE *GetRowBytes )( 
            IDeckLinkMutableVideoFrame * This);
        
        BMDPixelFormat ( STDMETHODCALLTYPE *GetPixelFormat )( 
            IDeckLinkMutableVideoFrame * This);
        
        BMDFrameFlags ( STDMETHODCALLTYPE *GetFlags )( 
            IDeckLinkMutableVideoFrame * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetBytes )( 
            IDeckLinkMutableVideoFrame * This,
            /* [out] */ void **buffer);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecode )( 
            IDeckLinkMutableVideoFrame * This,
            /* [in] */ BMDTimecodeFormat format,
            /* [out] */ IDeckLinkTimecode **timecode);
        
        HRESULT ( STDMETHODCALLTYPE *GetAncillaryData )( 
            IDeckLinkMutableVideoFrame * This,
            /* [out] */ IDeckLinkVideoFrameAncillary **ancillary);
        
        HRESULT ( STDMETHODCALLTYPE *SetFlags )( 
            IDeckLinkMutableVideoFrame * This,
            /* [in] */ BMDFrameFlags newFlags);
        
        HRESULT ( STDMETHODCALLTYPE *SetTimecode )( 
            IDeckLinkMutableVideoFrame * This,
            /* [in] */ BMDTimecodeFormat format,
            /* [in] */ IDeckLinkTimecode *timecode);
        
        HRESULT ( STDMETHODCALLTYPE *SetTimecodeFromComponents )( 
            IDeckLinkMutableVideoFrame * This,
            /* [in] */ BMDTimecodeFormat format,
            /* [in] */ unsigned char hours,
            /* [in] */ unsigned char minutes,
            /* [in] */ unsigned char seconds,
            /* [in] */ unsigned char frames,
            /* [in] */ BMDTimecodeFlags flags);
        
        HRESULT ( STDMETHODCALLTYPE *SetAncillaryData )( 
            IDeckLinkMutableVideoFrame * This,
            /* [in] */ IDeckLinkVideoFrameAncillary *ancillary);
        
        HRESULT ( STDMETHODCALLTYPE *SetTimecodeUserBits )( 
            IDeckLinkMutableVideoFrame * This,
            /* [in] */ BMDTimecodeFormat format,
            /* [in] */ BMDTimecodeUserBits userBits);
        
        END_INTERFACE
    } IDeckLinkMutableVideoFrameVtbl;

    interface IDeckLinkMutableVideoFrame
    {
        CONST_VTBL struct IDeckLinkMutableVideoFrameVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkMutableVideoFrame_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkMutableVideoFrame_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkMutableVideoFrame_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkMutableVideoFrame_GetWidth(This)	\
    ( (This)->lpVtbl -> GetWidth(This) ) 

#define IDeckLinkMutableVideoFrame_GetHeight(This)	\
    ( (This)->lpVtbl -> GetHeight(This) ) 

#define IDeckLinkMutableVideoFrame_GetRowBytes(This)	\
    ( (This)->lpVtbl -> GetRowBytes(This) ) 

#define IDeckLinkMutableVideoFrame_GetPixelFormat(This)	\
    ( (This)->lpVtbl -> GetPixelFormat(This) ) 

#define IDeckLinkMutableVideoFrame_GetFlags(This)	\
    ( (This)->lpVtbl -> GetFlags(This) ) 

#define IDeckLinkMutableVideoFrame_GetBytes(This,buffer)	\
    ( (This)->lpVtbl -> GetBytes(This,buffer) ) 

#define IDeckLinkMutableVideoFrame_GetTimecode(This,format,timecode)	\
    ( (This)->lpVtbl -> GetTimecode(This,format,timecode) ) 

#define IDeckLinkMutableVideoFrame_GetAncillaryData(This,ancillary)	\
    ( (This)->lpVtbl -> GetAncillaryData(This,ancillary) ) 


#define IDeckLinkMutableVideoFrame_SetFlags(This,newFlags)	\
    ( (This)->lpVtbl -> SetFlags(This,newFlags) ) 

#define IDeckLinkMutableVideoFrame_SetTimecode(This,format,timecode)	\
    ( (This)->lpVtbl -> SetTimecode(This,format,timecode) ) 

#define IDeckLinkMutableVideoFrame_SetTimecodeFromComponents(This,format,hours,minutes,seconds,frames,flags)	\
    ( (This)->lpVtbl -> SetTimecodeFromComponents(This,format,hours,minutes,seconds,frames,flags) ) 

#define IDeckLinkMutableVideoFrame_SetAncillaryData(This,ancillary)	\
    ( (This)->lpVtbl -> SetAncillaryData(This,ancillary) ) 

#define IDeckLinkMutableVideoFrame_SetTimecodeUserBits(This,format,userBits)	\
    ( (This)->lpVtbl -> SetTimecodeUserBits(This,format,userBits) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkMutableVideoFrame_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkVideoFrame3DExtensions_INTERFACE_DEFINED__
#define __IDeckLinkVideoFrame3DExtensions_INTERFACE_DEFINED__

/* interface IDeckLinkVideoFrame3DExtensions */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoFrame3DExtensions;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("DA0F7E4A-EDC7-48A8-9CDD-2DB51C729CD7")
    IDeckLinkVideoFrame3DExtensions : public IUnknown
    {
    public:
        virtual BMDVideo3DPackingFormat STDMETHODCALLTYPE Get3DPackingFormat( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetFrameForRightEye( 
            /* [out] */ IDeckLinkVideoFrame **rightEyeFrame) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoFrame3DExtensionsVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoFrame3DExtensions * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoFrame3DExtensions * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoFrame3DExtensions * This);
        
        BMDVideo3DPackingFormat ( STDMETHODCALLTYPE *Get3DPackingFormat )( 
            IDeckLinkVideoFrame3DExtensions * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetFrameForRightEye )( 
            IDeckLinkVideoFrame3DExtensions * This,
            /* [out] */ IDeckLinkVideoFrame **rightEyeFrame);
        
        END_INTERFACE
    } IDeckLinkVideoFrame3DExtensionsVtbl;

    interface IDeckLinkVideoFrame3DExtensions
    {
        CONST_VTBL struct IDeckLinkVideoFrame3DExtensionsVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoFrame3DExtensions_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoFrame3DExtensions_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoFrame3DExtensions_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoFrame3DExtensions_Get3DPackingFormat(This)	\
    ( (This)->lpVtbl -> Get3DPackingFormat(This) ) 

#define IDeckLinkVideoFrame3DExtensions_GetFrameForRightEye(This,rightEyeFrame)	\
    ( (This)->lpVtbl -> GetFrameForRightEye(This,rightEyeFrame) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoFrame3DExtensions_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkVideoInputFrame_INTERFACE_DEFINED__
#define __IDeckLinkVideoInputFrame_INTERFACE_DEFINED__

/* interface IDeckLinkVideoInputFrame */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoInputFrame;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("05CFE374-537C-4094-9A57-680525118F44")
    IDeckLinkVideoInputFrame : public IDeckLinkVideoFrame
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetStreamTime( 
            /* [out] */ BMDTimeValue *frameTime,
            /* [out] */ BMDTimeValue *frameDuration,
            /* [in] */ BMDTimeScale timeScale) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetHardwareReferenceTimestamp( 
            /* [in] */ BMDTimeScale timeScale,
            /* [out] */ BMDTimeValue *frameTime,
            /* [out] */ BMDTimeValue *frameDuration) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoInputFrameVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoInputFrame * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoInputFrame * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoInputFrame * This);
        
        long ( STDMETHODCALLTYPE *GetWidth )( 
            IDeckLinkVideoInputFrame * This);
        
        long ( STDMETHODCALLTYPE *GetHeight )( 
            IDeckLinkVideoInputFrame * This);
        
        long ( STDMETHODCALLTYPE *GetRowBytes )( 
            IDeckLinkVideoInputFrame * This);
        
        BMDPixelFormat ( STDMETHODCALLTYPE *GetPixelFormat )( 
            IDeckLinkVideoInputFrame * This);
        
        BMDFrameFlags ( STDMETHODCALLTYPE *GetFlags )( 
            IDeckLinkVideoInputFrame * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetBytes )( 
            IDeckLinkVideoInputFrame * This,
            /* [out] */ void **buffer);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecode )( 
            IDeckLinkVideoInputFrame * This,
            /* [in] */ BMDTimecodeFormat format,
            /* [out] */ IDeckLinkTimecode **timecode);
        
        HRESULT ( STDMETHODCALLTYPE *GetAncillaryData )( 
            IDeckLinkVideoInputFrame * This,
            /* [out] */ IDeckLinkVideoFrameAncillary **ancillary);
        
        HRESULT ( STDMETHODCALLTYPE *GetStreamTime )( 
            IDeckLinkVideoInputFrame * This,
            /* [out] */ BMDTimeValue *frameTime,
            /* [out] */ BMDTimeValue *frameDuration,
            /* [in] */ BMDTimeScale timeScale);
        
        HRESULT ( STDMETHODCALLTYPE *GetHardwareReferenceTimestamp )( 
            IDeckLinkVideoInputFrame * This,
            /* [in] */ BMDTimeScale timeScale,
            /* [out] */ BMDTimeValue *frameTime,
            /* [out] */ BMDTimeValue *frameDuration);
        
        END_INTERFACE
    } IDeckLinkVideoInputFrameVtbl;

    interface IDeckLinkVideoInputFrame
    {
        CONST_VTBL struct IDeckLinkVideoInputFrameVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoInputFrame_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoInputFrame_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoInputFrame_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoInputFrame_GetWidth(This)	\
    ( (This)->lpVtbl -> GetWidth(This) ) 

#define IDeckLinkVideoInputFrame_GetHeight(This)	\
    ( (This)->lpVtbl -> GetHeight(This) ) 

#define IDeckLinkVideoInputFrame_GetRowBytes(This)	\
    ( (This)->lpVtbl -> GetRowBytes(This) ) 

#define IDeckLinkVideoInputFrame_GetPixelFormat(This)	\
    ( (This)->lpVtbl -> GetPixelFormat(This) ) 

#define IDeckLinkVideoInputFrame_GetFlags(This)	\
    ( (This)->lpVtbl -> GetFlags(This) ) 

#define IDeckLinkVideoInputFrame_GetBytes(This,buffer)	\
    ( (This)->lpVtbl -> GetBytes(This,buffer) ) 

#define IDeckLinkVideoInputFrame_GetTimecode(This,format,timecode)	\
    ( (This)->lpVtbl -> GetTimecode(This,format,timecode) ) 

#define IDeckLinkVideoInputFrame_GetAncillaryData(This,ancillary)	\
    ( (This)->lpVtbl -> GetAncillaryData(This,ancillary) ) 


#define IDeckLinkVideoInputFrame_GetStreamTime(This,frameTime,frameDuration,timeScale)	\
    ( (This)->lpVtbl -> GetStreamTime(This,frameTime,frameDuration,timeScale) ) 

#define IDeckLinkVideoInputFrame_GetHardwareReferenceTimestamp(This,timeScale,frameTime,frameDuration)	\
    ( (This)->lpVtbl -> GetHardwareReferenceTimestamp(This,timeScale,frameTime,frameDuration) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoInputFrame_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkVideoFrameAncillary_INTERFACE_DEFINED__
#define __IDeckLinkVideoFrameAncillary_INTERFACE_DEFINED__

/* interface IDeckLinkVideoFrameAncillary */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoFrameAncillary;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("732E723C-D1A4-4E29-9E8E-4A88797A0004")
    IDeckLinkVideoFrameAncillary : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetBufferForVerticalBlankingLine( 
            /* [in] */ unsigned long lineNumber,
            /* [out] */ void **buffer) = 0;
        
        virtual BMDPixelFormat STDMETHODCALLTYPE GetPixelFormat( void) = 0;
        
        virtual BMDDisplayMode STDMETHODCALLTYPE GetDisplayMode( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoFrameAncillaryVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoFrameAncillary * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoFrameAncillary * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoFrameAncillary * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetBufferForVerticalBlankingLine )( 
            IDeckLinkVideoFrameAncillary * This,
            /* [in] */ unsigned long lineNumber,
            /* [out] */ void **buffer);
        
        BMDPixelFormat ( STDMETHODCALLTYPE *GetPixelFormat )( 
            IDeckLinkVideoFrameAncillary * This);
        
        BMDDisplayMode ( STDMETHODCALLTYPE *GetDisplayMode )( 
            IDeckLinkVideoFrameAncillary * This);
        
        END_INTERFACE
    } IDeckLinkVideoFrameAncillaryVtbl;

    interface IDeckLinkVideoFrameAncillary
    {
        CONST_VTBL struct IDeckLinkVideoFrameAncillaryVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoFrameAncillary_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoFrameAncillary_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoFrameAncillary_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoFrameAncillary_GetBufferForVerticalBlankingLine(This,lineNumber,buffer)	\
    ( (This)->lpVtbl -> GetBufferForVerticalBlankingLine(This,lineNumber,buffer) ) 

#define IDeckLinkVideoFrameAncillary_GetPixelFormat(This)	\
    ( (This)->lpVtbl -> GetPixelFormat(This) ) 

#define IDeckLinkVideoFrameAncillary_GetDisplayMode(This)	\
    ( (This)->lpVtbl -> GetDisplayMode(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoFrameAncillary_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkAudioInputPacket_INTERFACE_DEFINED__
#define __IDeckLinkAudioInputPacket_INTERFACE_DEFINED__

/* interface IDeckLinkAudioInputPacket */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkAudioInputPacket;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E43D5870-2894-11DE-8C30-0800200C9A66")
    IDeckLinkAudioInputPacket : public IUnknown
    {
    public:
        virtual long STDMETHODCALLTYPE GetSampleFrameCount( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBytes( 
            /* [out] */ void **buffer) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetPacketTime( 
            /* [out] */ BMDTimeValue *packetTime,
            /* [in] */ BMDTimeScale timeScale) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkAudioInputPacketVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkAudioInputPacket * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkAudioInputPacket * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkAudioInputPacket * This);
        
        long ( STDMETHODCALLTYPE *GetSampleFrameCount )( 
            IDeckLinkAudioInputPacket * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetBytes )( 
            IDeckLinkAudioInputPacket * This,
            /* [out] */ void **buffer);
        
        HRESULT ( STDMETHODCALLTYPE *GetPacketTime )( 
            IDeckLinkAudioInputPacket * This,
            /* [out] */ BMDTimeValue *packetTime,
            /* [in] */ BMDTimeScale timeScale);
        
        END_INTERFACE
    } IDeckLinkAudioInputPacketVtbl;

    interface IDeckLinkAudioInputPacket
    {
        CONST_VTBL struct IDeckLinkAudioInputPacketVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkAudioInputPacket_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkAudioInputPacket_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkAudioInputPacket_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkAudioInputPacket_GetSampleFrameCount(This)	\
    ( (This)->lpVtbl -> GetSampleFrameCount(This) ) 

#define IDeckLinkAudioInputPacket_GetBytes(This,buffer)	\
    ( (This)->lpVtbl -> GetBytes(This,buffer) ) 

#define IDeckLinkAudioInputPacket_GetPacketTime(This,packetTime,timeScale)	\
    ( (This)->lpVtbl -> GetPacketTime(This,packetTime,timeScale) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkAudioInputPacket_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkScreenPreviewCallback_INTERFACE_DEFINED__
#define __IDeckLinkScreenPreviewCallback_INTERFACE_DEFINED__

/* interface IDeckLinkScreenPreviewCallback */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkScreenPreviewCallback;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("B1D3F49A-85FE-4C5D-95C8-0B5D5DCCD438")
    IDeckLinkScreenPreviewCallback : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE DrawFrame( 
            /* [in] */ IDeckLinkVideoFrame *theFrame) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkScreenPreviewCallbackVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkScreenPreviewCallback * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkScreenPreviewCallback * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkScreenPreviewCallback * This);
        
        HRESULT ( STDMETHODCALLTYPE *DrawFrame )( 
            IDeckLinkScreenPreviewCallback * This,
            /* [in] */ IDeckLinkVideoFrame *theFrame);
        
        END_INTERFACE
    } IDeckLinkScreenPreviewCallbackVtbl;

    interface IDeckLinkScreenPreviewCallback
    {
        CONST_VTBL struct IDeckLinkScreenPreviewCallbackVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkScreenPreviewCallback_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkScreenPreviewCallback_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkScreenPreviewCallback_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkScreenPreviewCallback_DrawFrame(This,theFrame)	\
    ( (This)->lpVtbl -> DrawFrame(This,theFrame) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkScreenPreviewCallback_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkGLScreenPreviewHelper_INTERFACE_DEFINED__
#define __IDeckLinkGLScreenPreviewHelper_INTERFACE_DEFINED__

/* interface IDeckLinkGLScreenPreviewHelper */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkGLScreenPreviewHelper;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("504E2209-CAC7-4C1A-9FB4-C5BB6274D22F")
    IDeckLinkGLScreenPreviewHelper : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE InitializeGL( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE PaintGL( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetFrame( 
            /* [in] */ IDeckLinkVideoFrame *theFrame) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Set3DPreviewFormat( 
            /* [in] */ BMD3DPreviewFormat previewFormat) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkGLScreenPreviewHelperVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkGLScreenPreviewHelper * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkGLScreenPreviewHelper * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkGLScreenPreviewHelper * This);
        
        HRESULT ( STDMETHODCALLTYPE *InitializeGL )( 
            IDeckLinkGLScreenPreviewHelper * This);
        
        HRESULT ( STDMETHODCALLTYPE *PaintGL )( 
            IDeckLinkGLScreenPreviewHelper * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetFrame )( 
            IDeckLinkGLScreenPreviewHelper * This,
            /* [in] */ IDeckLinkVideoFrame *theFrame);
        
        HRESULT ( STDMETHODCALLTYPE *Set3DPreviewFormat )( 
            IDeckLinkGLScreenPreviewHelper * This,
            /* [in] */ BMD3DPreviewFormat previewFormat);
        
        END_INTERFACE
    } IDeckLinkGLScreenPreviewHelperVtbl;

    interface IDeckLinkGLScreenPreviewHelper
    {
        CONST_VTBL struct IDeckLinkGLScreenPreviewHelperVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkGLScreenPreviewHelper_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkGLScreenPreviewHelper_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkGLScreenPreviewHelper_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkGLScreenPreviewHelper_InitializeGL(This)	\
    ( (This)->lpVtbl -> InitializeGL(This) ) 

#define IDeckLinkGLScreenPreviewHelper_PaintGL(This)	\
    ( (This)->lpVtbl -> PaintGL(This) ) 

#define IDeckLinkGLScreenPreviewHelper_SetFrame(This,theFrame)	\
    ( (This)->lpVtbl -> SetFrame(This,theFrame) ) 

#define IDeckLinkGLScreenPreviewHelper_Set3DPreviewFormat(This,previewFormat)	\
    ( (This)->lpVtbl -> Set3DPreviewFormat(This,previewFormat) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkGLScreenPreviewHelper_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkConfiguration_INTERFACE_DEFINED__
#define __IDeckLinkConfiguration_INTERFACE_DEFINED__

/* interface IDeckLinkConfiguration */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkConfiguration;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("C679A35B-610C-4D09-B748-1D0478100FC0")
    IDeckLinkConfiguration : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE SetFlag( 
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [in] */ BOOL value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetFlag( 
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [out] */ BOOL *value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetInt( 
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [in] */ LONGLONG value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetInt( 
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [out] */ LONGLONG *value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetFloat( 
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [in] */ double value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetFloat( 
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [out] */ double *value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetString( 
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [in] */ BSTR value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetString( 
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [out] */ BSTR *value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE WriteConfigurationToPreferences( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkConfigurationVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkConfiguration * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkConfiguration * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkConfiguration * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetFlag )( 
            IDeckLinkConfiguration * This,
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [in] */ BOOL value);
        
        HRESULT ( STDMETHODCALLTYPE *GetFlag )( 
            IDeckLinkConfiguration * This,
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [out] */ BOOL *value);
        
        HRESULT ( STDMETHODCALLTYPE *SetInt )( 
            IDeckLinkConfiguration * This,
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [in] */ LONGLONG value);
        
        HRESULT ( STDMETHODCALLTYPE *GetInt )( 
            IDeckLinkConfiguration * This,
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [out] */ LONGLONG *value);
        
        HRESULT ( STDMETHODCALLTYPE *SetFloat )( 
            IDeckLinkConfiguration * This,
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [in] */ double value);
        
        HRESULT ( STDMETHODCALLTYPE *GetFloat )( 
            IDeckLinkConfiguration * This,
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [out] */ double *value);
        
        HRESULT ( STDMETHODCALLTYPE *SetString )( 
            IDeckLinkConfiguration * This,
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [in] */ BSTR value);
        
        HRESULT ( STDMETHODCALLTYPE *GetString )( 
            IDeckLinkConfiguration * This,
            /* [in] */ BMDDeckLinkConfigurationID cfgID,
            /* [out] */ BSTR *value);
        
        HRESULT ( STDMETHODCALLTYPE *WriteConfigurationToPreferences )( 
            IDeckLinkConfiguration * This);
        
        END_INTERFACE
    } IDeckLinkConfigurationVtbl;

    interface IDeckLinkConfiguration
    {
        CONST_VTBL struct IDeckLinkConfigurationVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkConfiguration_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkConfiguration_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkConfiguration_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkConfiguration_SetFlag(This,cfgID,value)	\
    ( (This)->lpVtbl -> SetFlag(This,cfgID,value) ) 

#define IDeckLinkConfiguration_GetFlag(This,cfgID,value)	\
    ( (This)->lpVtbl -> GetFlag(This,cfgID,value) ) 

#define IDeckLinkConfiguration_SetInt(This,cfgID,value)	\
    ( (This)->lpVtbl -> SetInt(This,cfgID,value) ) 

#define IDeckLinkConfiguration_GetInt(This,cfgID,value)	\
    ( (This)->lpVtbl -> GetInt(This,cfgID,value) ) 

#define IDeckLinkConfiguration_SetFloat(This,cfgID,value)	\
    ( (This)->lpVtbl -> SetFloat(This,cfgID,value) ) 

#define IDeckLinkConfiguration_GetFloat(This,cfgID,value)	\
    ( (This)->lpVtbl -> GetFloat(This,cfgID,value) ) 

#define IDeckLinkConfiguration_SetString(This,cfgID,value)	\
    ( (This)->lpVtbl -> SetString(This,cfgID,value) ) 

#define IDeckLinkConfiguration_GetString(This,cfgID,value)	\
    ( (This)->lpVtbl -> GetString(This,cfgID,value) ) 

#define IDeckLinkConfiguration_WriteConfigurationToPreferences(This)	\
    ( (This)->lpVtbl -> WriteConfigurationToPreferences(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkConfiguration_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkAttributes_INTERFACE_DEFINED__
#define __IDeckLinkAttributes_INTERFACE_DEFINED__

/* interface IDeckLinkAttributes */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkAttributes;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("ABC11843-D966-44CB-96E2-A1CB5D3135C4")
    IDeckLinkAttributes : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetFlag( 
            /* [in] */ BMDDeckLinkAttributeID cfgID,
            /* [out] */ BOOL *value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetInt( 
            /* [in] */ BMDDeckLinkAttributeID cfgID,
            /* [out] */ LONGLONG *value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetFloat( 
            /* [in] */ BMDDeckLinkAttributeID cfgID,
            /* [out] */ double *value) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetString( 
            /* [in] */ BMDDeckLinkAttributeID cfgID,
            /* [out] */ BSTR *value) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkAttributesVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkAttributes * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkAttributes * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkAttributes * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetFlag )( 
            IDeckLinkAttributes * This,
            /* [in] */ BMDDeckLinkAttributeID cfgID,
            /* [out] */ BOOL *value);
        
        HRESULT ( STDMETHODCALLTYPE *GetInt )( 
            IDeckLinkAttributes * This,
            /* [in] */ BMDDeckLinkAttributeID cfgID,
            /* [out] */ LONGLONG *value);
        
        HRESULT ( STDMETHODCALLTYPE *GetFloat )( 
            IDeckLinkAttributes * This,
            /* [in] */ BMDDeckLinkAttributeID cfgID,
            /* [out] */ double *value);
        
        HRESULT ( STDMETHODCALLTYPE *GetString )( 
            IDeckLinkAttributes * This,
            /* [in] */ BMDDeckLinkAttributeID cfgID,
            /* [out] */ BSTR *value);
        
        END_INTERFACE
    } IDeckLinkAttributesVtbl;

    interface IDeckLinkAttributes
    {
        CONST_VTBL struct IDeckLinkAttributesVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkAttributes_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkAttributes_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkAttributes_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkAttributes_GetFlag(This,cfgID,value)	\
    ( (This)->lpVtbl -> GetFlag(This,cfgID,value) ) 

#define IDeckLinkAttributes_GetInt(This,cfgID,value)	\
    ( (This)->lpVtbl -> GetInt(This,cfgID,value) ) 

#define IDeckLinkAttributes_GetFloat(This,cfgID,value)	\
    ( (This)->lpVtbl -> GetFloat(This,cfgID,value) ) 

#define IDeckLinkAttributes_GetString(This,cfgID,value)	\
    ( (This)->lpVtbl -> GetString(This,cfgID,value) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkAttributes_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkKeyer_INTERFACE_DEFINED__
#define __IDeckLinkKeyer_INTERFACE_DEFINED__

/* interface IDeckLinkKeyer */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkKeyer;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("89AFCAF5-65F8-421E-98F7-96FE5F5BFBA3")
    IDeckLinkKeyer : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE Enable( 
            /* [in] */ BOOL isExternal) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetLevel( 
            /* [in] */ unsigned char level) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE RampUp( 
            /* [in] */ unsigned long numberOfFrames) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE RampDown( 
            /* [in] */ unsigned long numberOfFrames) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Disable( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkKeyerVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkKeyer * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkKeyer * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkKeyer * This);
        
        HRESULT ( STDMETHODCALLTYPE *Enable )( 
            IDeckLinkKeyer * This,
            /* [in] */ BOOL isExternal);
        
        HRESULT ( STDMETHODCALLTYPE *SetLevel )( 
            IDeckLinkKeyer * This,
            /* [in] */ unsigned char level);
        
        HRESULT ( STDMETHODCALLTYPE *RampUp )( 
            IDeckLinkKeyer * This,
            /* [in] */ unsigned long numberOfFrames);
        
        HRESULT ( STDMETHODCALLTYPE *RampDown )( 
            IDeckLinkKeyer * This,
            /* [in] */ unsigned long numberOfFrames);
        
        HRESULT ( STDMETHODCALLTYPE *Disable )( 
            IDeckLinkKeyer * This);
        
        END_INTERFACE
    } IDeckLinkKeyerVtbl;

    interface IDeckLinkKeyer
    {
        CONST_VTBL struct IDeckLinkKeyerVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkKeyer_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkKeyer_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkKeyer_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkKeyer_Enable(This,isExternal)	\
    ( (This)->lpVtbl -> Enable(This,isExternal) ) 

#define IDeckLinkKeyer_SetLevel(This,level)	\
    ( (This)->lpVtbl -> SetLevel(This,level) ) 

#define IDeckLinkKeyer_RampUp(This,numberOfFrames)	\
    ( (This)->lpVtbl -> RampUp(This,numberOfFrames) ) 

#define IDeckLinkKeyer_RampDown(This,numberOfFrames)	\
    ( (This)->lpVtbl -> RampDown(This,numberOfFrames) ) 

#define IDeckLinkKeyer_Disable(This)	\
    ( (This)->lpVtbl -> Disable(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkKeyer_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkVideoConversion_INTERFACE_DEFINED__
#define __IDeckLinkVideoConversion_INTERFACE_DEFINED__

/* interface IDeckLinkVideoConversion */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoConversion;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("3BBCB8A2-DA2C-42D9-B5D8-88083644E99A")
    IDeckLinkVideoConversion : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE ConvertFrame( 
            /* [in] */ IDeckLinkVideoFrame *srcFrame,
            /* [in] */ IDeckLinkVideoFrame *dstFrame) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoConversionVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoConversion * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoConversion * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoConversion * This);
        
        HRESULT ( STDMETHODCALLTYPE *ConvertFrame )( 
            IDeckLinkVideoConversion * This,
            /* [in] */ IDeckLinkVideoFrame *srcFrame,
            /* [in] */ IDeckLinkVideoFrame *dstFrame);
        
        END_INTERFACE
    } IDeckLinkVideoConversionVtbl;

    interface IDeckLinkVideoConversion
    {
        CONST_VTBL struct IDeckLinkVideoConversionVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoConversion_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoConversion_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoConversion_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoConversion_ConvertFrame(This,srcFrame,dstFrame)	\
    ( (This)->lpVtbl -> ConvertFrame(This,srcFrame,dstFrame) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoConversion_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkDeckControlStatusCallback_INTERFACE_DEFINED__
#define __IDeckLinkDeckControlStatusCallback_INTERFACE_DEFINED__

/* interface IDeckLinkDeckControlStatusCallback */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkDeckControlStatusCallback;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E5F693C1-4283-4716-B18F-C1431521955B")
    IDeckLinkDeckControlStatusCallback : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE TimecodeUpdate( 
            /* [in] */ BMDTimecodeBCD currentTimecode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE VTRControlStateChanged( 
            /* [in] */ BMDDeckControlVTRControlState newState,
            /* [in] */ BMDDeckControlError error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DeckControlEventReceived( 
            /* [in] */ BMDDeckControlEvent event,
            /* [in] */ BMDDeckControlError error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DeckControlStatusChanged( 
            /* [in] */ BMDDeckControlStatusFlags flags,
            /* [in] */ unsigned long mask) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkDeckControlStatusCallbackVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkDeckControlStatusCallback * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkDeckControlStatusCallback * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkDeckControlStatusCallback * This);
        
        HRESULT ( STDMETHODCALLTYPE *TimecodeUpdate )( 
            IDeckLinkDeckControlStatusCallback * This,
            /* [in] */ BMDTimecodeBCD currentTimecode);
        
        HRESULT ( STDMETHODCALLTYPE *VTRControlStateChanged )( 
            IDeckLinkDeckControlStatusCallback * This,
            /* [in] */ BMDDeckControlVTRControlState newState,
            /* [in] */ BMDDeckControlError error);
        
        HRESULT ( STDMETHODCALLTYPE *DeckControlEventReceived )( 
            IDeckLinkDeckControlStatusCallback * This,
            /* [in] */ BMDDeckControlEvent event,
            /* [in] */ BMDDeckControlError error);
        
        HRESULT ( STDMETHODCALLTYPE *DeckControlStatusChanged )( 
            IDeckLinkDeckControlStatusCallback * This,
            /* [in] */ BMDDeckControlStatusFlags flags,
            /* [in] */ unsigned long mask);
        
        END_INTERFACE
    } IDeckLinkDeckControlStatusCallbackVtbl;

    interface IDeckLinkDeckControlStatusCallback
    {
        CONST_VTBL struct IDeckLinkDeckControlStatusCallbackVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkDeckControlStatusCallback_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkDeckControlStatusCallback_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkDeckControlStatusCallback_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkDeckControlStatusCallback_TimecodeUpdate(This,currentTimecode)	\
    ( (This)->lpVtbl -> TimecodeUpdate(This,currentTimecode) ) 

#define IDeckLinkDeckControlStatusCallback_VTRControlStateChanged(This,newState,error)	\
    ( (This)->lpVtbl -> VTRControlStateChanged(This,newState,error) ) 

#define IDeckLinkDeckControlStatusCallback_DeckControlEventReceived(This,event,error)	\
    ( (This)->lpVtbl -> DeckControlEventReceived(This,event,error) ) 

#define IDeckLinkDeckControlStatusCallback_DeckControlStatusChanged(This,flags,mask)	\
    ( (This)->lpVtbl -> DeckControlStatusChanged(This,flags,mask) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkDeckControlStatusCallback_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkDeckControl_INTERFACE_DEFINED__
#define __IDeckLinkDeckControl_INTERFACE_DEFINED__

/* interface IDeckLinkDeckControl */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkDeckControl;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("522A9E39-0F3C-4742-94EE-D80DE335DA1D")
    IDeckLinkDeckControl : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE Open( 
            /* [in] */ BMDTimeScale timeScale,
            /* [in] */ BMDTimeValue timeValue,
            /* [in] */ BOOL timecodeIsDropFrame,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Close( 
            /* [in] */ BOOL standbyOn) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetCurrentState( 
            /* [out] */ BMDDeckControlMode *mode,
            /* [out] */ BMDDeckControlVTRControlState *vtrControlState,
            /* [out] */ BMDDeckControlStatusFlags *flags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetStandby( 
            /* [in] */ BOOL standbyOn) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SendCommand( 
            /* [in] */ unsigned char *inBuffer,
            /* [in] */ unsigned long inBufferSize,
            /* [out] */ unsigned char *outBuffer,
            /* [out] */ unsigned long *outDataSize,
            /* [in] */ unsigned long outBufferSize,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Play( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Stop( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE TogglePlayStop( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Eject( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GoToTimecode( 
            /* [in] */ BMDTimecodeBCD timecode,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE FastForward( 
            /* [in] */ BOOL viewTape,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Rewind( 
            /* [in] */ BOOL viewTape,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StepForward( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StepBack( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Jog( 
            /* [in] */ double rate,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Shuttle( 
            /* [in] */ double rate,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetTimecodeString( 
            /* [out] */ BSTR *currentTimeCode,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetTimecode( 
            /* [out] */ IDeckLinkTimecode **currentTimecode,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetTimecodeBCD( 
            /* [out] */ BMDTimecodeBCD *currentTimecode,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetPreroll( 
            /* [in] */ unsigned long prerollSeconds) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetPreroll( 
            /* [out] */ unsigned long *prerollSeconds) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetExportOffset( 
            /* [in] */ long exportOffsetFields) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetExportOffset( 
            /* [out] */ long *exportOffsetFields) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetManualExportOffset( 
            /* [out] */ long *deckManualExportOffsetFields) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetCaptureOffset( 
            /* [in] */ long captureOffsetFields) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetCaptureOffset( 
            /* [out] */ long *captureOffsetFields) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StartExport( 
            /* [in] */ BMDTimecodeBCD inTimecode,
            /* [in] */ BMDTimecodeBCD outTimecode,
            /* [in] */ BMDDeckControlExportModeOpsFlags exportModeOps,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StartCapture( 
            /* [in] */ BOOL useVITC,
            /* [in] */ BMDTimecodeBCD inTimecode,
            /* [in] */ BMDTimecodeBCD outTimecode,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetDeviceID( 
            /* [out] */ unsigned short *deviceId,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Abort( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE CrashRecordStart( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE CrashRecordStop( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetCallback( 
            /* [in] */ IDeckLinkDeckControlStatusCallback *callback) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkDeckControlVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkDeckControl * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkDeckControl * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkDeckControl * This);
        
        HRESULT ( STDMETHODCALLTYPE *Open )( 
            IDeckLinkDeckControl * This,
            /* [in] */ BMDTimeScale timeScale,
            /* [in] */ BMDTimeValue timeValue,
            /* [in] */ BOOL timecodeIsDropFrame,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Close )( 
            IDeckLinkDeckControl * This,
            /* [in] */ BOOL standbyOn);
        
        HRESULT ( STDMETHODCALLTYPE *GetCurrentState )( 
            IDeckLinkDeckControl * This,
            /* [out] */ BMDDeckControlMode *mode,
            /* [out] */ BMDDeckControlVTRControlState *vtrControlState,
            /* [out] */ BMDDeckControlStatusFlags *flags);
        
        HRESULT ( STDMETHODCALLTYPE *SetStandby )( 
            IDeckLinkDeckControl * This,
            /* [in] */ BOOL standbyOn);
        
        HRESULT ( STDMETHODCALLTYPE *SendCommand )( 
            IDeckLinkDeckControl * This,
            /* [in] */ unsigned char *inBuffer,
            /* [in] */ unsigned long inBufferSize,
            /* [out] */ unsigned char *outBuffer,
            /* [out] */ unsigned long *outDataSize,
            /* [in] */ unsigned long outBufferSize,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Play )( 
            IDeckLinkDeckControl * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Stop )( 
            IDeckLinkDeckControl * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *TogglePlayStop )( 
            IDeckLinkDeckControl * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Eject )( 
            IDeckLinkDeckControl * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *GoToTimecode )( 
            IDeckLinkDeckControl * This,
            /* [in] */ BMDTimecodeBCD timecode,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *FastForward )( 
            IDeckLinkDeckControl * This,
            /* [in] */ BOOL viewTape,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Rewind )( 
            IDeckLinkDeckControl * This,
            /* [in] */ BOOL viewTape,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *StepForward )( 
            IDeckLinkDeckControl * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *StepBack )( 
            IDeckLinkDeckControl * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Jog )( 
            IDeckLinkDeckControl * This,
            /* [in] */ double rate,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Shuttle )( 
            IDeckLinkDeckControl * This,
            /* [in] */ double rate,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecodeString )( 
            IDeckLinkDeckControl * This,
            /* [out] */ BSTR *currentTimeCode,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecode )( 
            IDeckLinkDeckControl * This,
            /* [out] */ IDeckLinkTimecode **currentTimecode,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecodeBCD )( 
            IDeckLinkDeckControl * This,
            /* [out] */ BMDTimecodeBCD *currentTimecode,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *SetPreroll )( 
            IDeckLinkDeckControl * This,
            /* [in] */ unsigned long prerollSeconds);
        
        HRESULT ( STDMETHODCALLTYPE *GetPreroll )( 
            IDeckLinkDeckControl * This,
            /* [out] */ unsigned long *prerollSeconds);
        
        HRESULT ( STDMETHODCALLTYPE *SetExportOffset )( 
            IDeckLinkDeckControl * This,
            /* [in] */ long exportOffsetFields);
        
        HRESULT ( STDMETHODCALLTYPE *GetExportOffset )( 
            IDeckLinkDeckControl * This,
            /* [out] */ long *exportOffsetFields);
        
        HRESULT ( STDMETHODCALLTYPE *GetManualExportOffset )( 
            IDeckLinkDeckControl * This,
            /* [out] */ long *deckManualExportOffsetFields);
        
        HRESULT ( STDMETHODCALLTYPE *SetCaptureOffset )( 
            IDeckLinkDeckControl * This,
            /* [in] */ long captureOffsetFields);
        
        HRESULT ( STDMETHODCALLTYPE *GetCaptureOffset )( 
            IDeckLinkDeckControl * This,
            /* [out] */ long *captureOffsetFields);
        
        HRESULT ( STDMETHODCALLTYPE *StartExport )( 
            IDeckLinkDeckControl * This,
            /* [in] */ BMDTimecodeBCD inTimecode,
            /* [in] */ BMDTimecodeBCD outTimecode,
            /* [in] */ BMDDeckControlExportModeOpsFlags exportModeOps,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *StartCapture )( 
            IDeckLinkDeckControl * This,
            /* [in] */ BOOL useVITC,
            /* [in] */ BMDTimecodeBCD inTimecode,
            /* [in] */ BMDTimecodeBCD outTimecode,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *GetDeviceID )( 
            IDeckLinkDeckControl * This,
            /* [out] */ unsigned short *deviceId,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Abort )( 
            IDeckLinkDeckControl * This);
        
        HRESULT ( STDMETHODCALLTYPE *CrashRecordStart )( 
            IDeckLinkDeckControl * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *CrashRecordStop )( 
            IDeckLinkDeckControl * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *SetCallback )( 
            IDeckLinkDeckControl * This,
            /* [in] */ IDeckLinkDeckControlStatusCallback *callback);
        
        END_INTERFACE
    } IDeckLinkDeckControlVtbl;

    interface IDeckLinkDeckControl
    {
        CONST_VTBL struct IDeckLinkDeckControlVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkDeckControl_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkDeckControl_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkDeckControl_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkDeckControl_Open(This,timeScale,timeValue,timecodeIsDropFrame,error)	\
    ( (This)->lpVtbl -> Open(This,timeScale,timeValue,timecodeIsDropFrame,error) ) 

#define IDeckLinkDeckControl_Close(This,standbyOn)	\
    ( (This)->lpVtbl -> Close(This,standbyOn) ) 

#define IDeckLinkDeckControl_GetCurrentState(This,mode,vtrControlState,flags)	\
    ( (This)->lpVtbl -> GetCurrentState(This,mode,vtrControlState,flags) ) 

#define IDeckLinkDeckControl_SetStandby(This,standbyOn)	\
    ( (This)->lpVtbl -> SetStandby(This,standbyOn) ) 

#define IDeckLinkDeckControl_SendCommand(This,inBuffer,inBufferSize,outBuffer,outDataSize,outBufferSize,error)	\
    ( (This)->lpVtbl -> SendCommand(This,inBuffer,inBufferSize,outBuffer,outDataSize,outBufferSize,error) ) 

#define IDeckLinkDeckControl_Play(This,error)	\
    ( (This)->lpVtbl -> Play(This,error) ) 

#define IDeckLinkDeckControl_Stop(This,error)	\
    ( (This)->lpVtbl -> Stop(This,error) ) 

#define IDeckLinkDeckControl_TogglePlayStop(This,error)	\
    ( (This)->lpVtbl -> TogglePlayStop(This,error) ) 

#define IDeckLinkDeckControl_Eject(This,error)	\
    ( (This)->lpVtbl -> Eject(This,error) ) 

#define IDeckLinkDeckControl_GoToTimecode(This,timecode,error)	\
    ( (This)->lpVtbl -> GoToTimecode(This,timecode,error) ) 

#define IDeckLinkDeckControl_FastForward(This,viewTape,error)	\
    ( (This)->lpVtbl -> FastForward(This,viewTape,error) ) 

#define IDeckLinkDeckControl_Rewind(This,viewTape,error)	\
    ( (This)->lpVtbl -> Rewind(This,viewTape,error) ) 

#define IDeckLinkDeckControl_StepForward(This,error)	\
    ( (This)->lpVtbl -> StepForward(This,error) ) 

#define IDeckLinkDeckControl_StepBack(This,error)	\
    ( (This)->lpVtbl -> StepBack(This,error) ) 

#define IDeckLinkDeckControl_Jog(This,rate,error)	\
    ( (This)->lpVtbl -> Jog(This,rate,error) ) 

#define IDeckLinkDeckControl_Shuttle(This,rate,error)	\
    ( (This)->lpVtbl -> Shuttle(This,rate,error) ) 

#define IDeckLinkDeckControl_GetTimecodeString(This,currentTimeCode,error)	\
    ( (This)->lpVtbl -> GetTimecodeString(This,currentTimeCode,error) ) 

#define IDeckLinkDeckControl_GetTimecode(This,currentTimecode,error)	\
    ( (This)->lpVtbl -> GetTimecode(This,currentTimecode,error) ) 

#define IDeckLinkDeckControl_GetTimecodeBCD(This,currentTimecode,error)	\
    ( (This)->lpVtbl -> GetTimecodeBCD(This,currentTimecode,error) ) 

#define IDeckLinkDeckControl_SetPreroll(This,prerollSeconds)	\
    ( (This)->lpVtbl -> SetPreroll(This,prerollSeconds) ) 

#define IDeckLinkDeckControl_GetPreroll(This,prerollSeconds)	\
    ( (This)->lpVtbl -> GetPreroll(This,prerollSeconds) ) 

#define IDeckLinkDeckControl_SetExportOffset(This,exportOffsetFields)	\
    ( (This)->lpVtbl -> SetExportOffset(This,exportOffsetFields) ) 

#define IDeckLinkDeckControl_GetExportOffset(This,exportOffsetFields)	\
    ( (This)->lpVtbl -> GetExportOffset(This,exportOffsetFields) ) 

#define IDeckLinkDeckControl_GetManualExportOffset(This,deckManualExportOffsetFields)	\
    ( (This)->lpVtbl -> GetManualExportOffset(This,deckManualExportOffsetFields) ) 

#define IDeckLinkDeckControl_SetCaptureOffset(This,captureOffsetFields)	\
    ( (This)->lpVtbl -> SetCaptureOffset(This,captureOffsetFields) ) 

#define IDeckLinkDeckControl_GetCaptureOffset(This,captureOffsetFields)	\
    ( (This)->lpVtbl -> GetCaptureOffset(This,captureOffsetFields) ) 

#define IDeckLinkDeckControl_StartExport(This,inTimecode,outTimecode,exportModeOps,error)	\
    ( (This)->lpVtbl -> StartExport(This,inTimecode,outTimecode,exportModeOps,error) ) 

#define IDeckLinkDeckControl_StartCapture(This,useVITC,inTimecode,outTimecode,error)	\
    ( (This)->lpVtbl -> StartCapture(This,useVITC,inTimecode,outTimecode,error) ) 

#define IDeckLinkDeckControl_GetDeviceID(This,deviceId,error)	\
    ( (This)->lpVtbl -> GetDeviceID(This,deviceId,error) ) 

#define IDeckLinkDeckControl_Abort(This)	\
    ( (This)->lpVtbl -> Abort(This) ) 

#define IDeckLinkDeckControl_CrashRecordStart(This,error)	\
    ( (This)->lpVtbl -> CrashRecordStart(This,error) ) 

#define IDeckLinkDeckControl_CrashRecordStop(This,error)	\
    ( (This)->lpVtbl -> CrashRecordStop(This,error) ) 

#define IDeckLinkDeckControl_SetCallback(This,callback)	\
    ( (This)->lpVtbl -> SetCallback(This,callback) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkDeckControl_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_CDeckLinkIterator;

#ifdef __cplusplus

class DECLSPEC_UUID("D9EDA3B3-2887-41FA-B724-017CF1EB1D37")
CDeckLinkIterator;
#endif

EXTERN_C const CLSID CLSID_CDeckLinkAPIInformation;

#ifdef __cplusplus

class DECLSPEC_UUID("263CA19F-ED09-482E-9F9D-84005783A237")
CDeckLinkAPIInformation;
#endif

EXTERN_C const CLSID CLSID_CDeckLinkGLScreenPreviewHelper;

#ifdef __cplusplus

class DECLSPEC_UUID("F63E77C7-B655-4A4A-9AD0-3CA85D394343")
CDeckLinkGLScreenPreviewHelper;
#endif

EXTERN_C const CLSID CLSID_CDeckLinkVideoConversion;

#ifdef __cplusplus

class DECLSPEC_UUID("7DBBBB11-5B7B-467D-AEA4-CEA468FD368C")
CDeckLinkVideoConversion;
#endif

#ifndef __IDeckLinkDeckControl_v7_9_INTERFACE_DEFINED__
#define __IDeckLinkDeckControl_v7_9_INTERFACE_DEFINED__

/* interface IDeckLinkDeckControl_v7_9 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkDeckControl_v7_9;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("A4D81043-0619-42B7-8ED6-602D29041DF7")
    IDeckLinkDeckControl_v7_9 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE Open( 
            /* [in] */ BMDTimeScale timeScale,
            /* [in] */ BMDTimeValue timeValue,
            /* [in] */ BOOL timecodeIsDropFrame,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Close( 
            /* [in] */ BOOL standbyOn) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetCurrentState( 
            /* [out] */ BMDDeckControlMode *mode,
            /* [out] */ BMDDeckControlVTRControlState *vtrControlState,
            /* [out] */ BMDDeckControlStatusFlags *flags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetStandby( 
            /* [in] */ BOOL standbyOn) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Play( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Stop( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE TogglePlayStop( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Eject( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GoToTimecode( 
            /* [in] */ BMDTimecodeBCD timecode,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE FastForward( 
            /* [in] */ BOOL viewTape,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Rewind( 
            /* [in] */ BOOL viewTape,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StepForward( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StepBack( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Jog( 
            /* [in] */ double rate,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Shuttle( 
            /* [in] */ double rate,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetTimecodeString( 
            /* [out] */ BSTR *currentTimeCode,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetTimecode( 
            /* [out] */ IDeckLinkTimecode **currentTimecode,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetTimecodeBCD( 
            /* [out] */ BMDTimecodeBCD *currentTimecode,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetPreroll( 
            /* [in] */ unsigned long prerollSeconds) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetPreroll( 
            /* [out] */ unsigned long *prerollSeconds) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetExportOffset( 
            /* [in] */ long exportOffsetFields) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetExportOffset( 
            /* [out] */ long *exportOffsetFields) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetManualExportOffset( 
            /* [out] */ long *deckManualExportOffsetFields) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetCaptureOffset( 
            /* [in] */ long captureOffsetFields) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetCaptureOffset( 
            /* [out] */ long *captureOffsetFields) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StartExport( 
            /* [in] */ BMDTimecodeBCD inTimecode,
            /* [in] */ BMDTimecodeBCD outTimecode,
            /* [in] */ BMDDeckControlExportModeOpsFlags exportModeOps,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StartCapture( 
            /* [in] */ BOOL useVITC,
            /* [in] */ BMDTimecodeBCD inTimecode,
            /* [in] */ BMDTimecodeBCD outTimecode,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetDeviceID( 
            /* [out] */ unsigned short *deviceId,
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Abort( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE CrashRecordStart( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE CrashRecordStop( 
            /* [out] */ BMDDeckControlError *error) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetCallback( 
            /* [in] */ IDeckLinkDeckControlStatusCallback *callback) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkDeckControl_v7_9Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkDeckControl_v7_9 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkDeckControl_v7_9 * This);
        
        HRESULT ( STDMETHODCALLTYPE *Open )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ BMDTimeScale timeScale,
            /* [in] */ BMDTimeValue timeValue,
            /* [in] */ BOOL timecodeIsDropFrame,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Close )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ BOOL standbyOn);
        
        HRESULT ( STDMETHODCALLTYPE *GetCurrentState )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ BMDDeckControlMode *mode,
            /* [out] */ BMDDeckControlVTRControlState *vtrControlState,
            /* [out] */ BMDDeckControlStatusFlags *flags);
        
        HRESULT ( STDMETHODCALLTYPE *SetStandby )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ BOOL standbyOn);
        
        HRESULT ( STDMETHODCALLTYPE *Play )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Stop )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *TogglePlayStop )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Eject )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *GoToTimecode )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ BMDTimecodeBCD timecode,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *FastForward )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ BOOL viewTape,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Rewind )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ BOOL viewTape,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *StepForward )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *StepBack )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Jog )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ double rate,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Shuttle )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ double rate,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecodeString )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ BSTR *currentTimeCode,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecode )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ IDeckLinkTimecode **currentTimecode,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecodeBCD )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ BMDTimecodeBCD *currentTimecode,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *SetPreroll )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ unsigned long prerollSeconds);
        
        HRESULT ( STDMETHODCALLTYPE *GetPreroll )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ unsigned long *prerollSeconds);
        
        HRESULT ( STDMETHODCALLTYPE *SetExportOffset )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ long exportOffsetFields);
        
        HRESULT ( STDMETHODCALLTYPE *GetExportOffset )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ long *exportOffsetFields);
        
        HRESULT ( STDMETHODCALLTYPE *GetManualExportOffset )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ long *deckManualExportOffsetFields);
        
        HRESULT ( STDMETHODCALLTYPE *SetCaptureOffset )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ long captureOffsetFields);
        
        HRESULT ( STDMETHODCALLTYPE *GetCaptureOffset )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ long *captureOffsetFields);
        
        HRESULT ( STDMETHODCALLTYPE *StartExport )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ BMDTimecodeBCD inTimecode,
            /* [in] */ BMDTimecodeBCD outTimecode,
            /* [in] */ BMDDeckControlExportModeOpsFlags exportModeOps,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *StartCapture )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ BOOL useVITC,
            /* [in] */ BMDTimecodeBCD inTimecode,
            /* [in] */ BMDTimecodeBCD outTimecode,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *GetDeviceID )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ unsigned short *deviceId,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *Abort )( 
            IDeckLinkDeckControl_v7_9 * This);
        
        HRESULT ( STDMETHODCALLTYPE *CrashRecordStart )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *CrashRecordStop )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [out] */ BMDDeckControlError *error);
        
        HRESULT ( STDMETHODCALLTYPE *SetCallback )( 
            IDeckLinkDeckControl_v7_9 * This,
            /* [in] */ IDeckLinkDeckControlStatusCallback *callback);
        
        END_INTERFACE
    } IDeckLinkDeckControl_v7_9Vtbl;

    interface IDeckLinkDeckControl_v7_9
    {
        CONST_VTBL struct IDeckLinkDeckControl_v7_9Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkDeckControl_v7_9_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkDeckControl_v7_9_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkDeckControl_v7_9_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkDeckControl_v7_9_Open(This,timeScale,timeValue,timecodeIsDropFrame,error)	\
    ( (This)->lpVtbl -> Open(This,timeScale,timeValue,timecodeIsDropFrame,error) ) 

#define IDeckLinkDeckControl_v7_9_Close(This,standbyOn)	\
    ( (This)->lpVtbl -> Close(This,standbyOn) ) 

#define IDeckLinkDeckControl_v7_9_GetCurrentState(This,mode,vtrControlState,flags)	\
    ( (This)->lpVtbl -> GetCurrentState(This,mode,vtrControlState,flags) ) 

#define IDeckLinkDeckControl_v7_9_SetStandby(This,standbyOn)	\
    ( (This)->lpVtbl -> SetStandby(This,standbyOn) ) 

#define IDeckLinkDeckControl_v7_9_Play(This,error)	\
    ( (This)->lpVtbl -> Play(This,error) ) 

#define IDeckLinkDeckControl_v7_9_Stop(This,error)	\
    ( (This)->lpVtbl -> Stop(This,error) ) 

#define IDeckLinkDeckControl_v7_9_TogglePlayStop(This,error)	\
    ( (This)->lpVtbl -> TogglePlayStop(This,error) ) 

#define IDeckLinkDeckControl_v7_9_Eject(This,error)	\
    ( (This)->lpVtbl -> Eject(This,error) ) 

#define IDeckLinkDeckControl_v7_9_GoToTimecode(This,timecode,error)	\
    ( (This)->lpVtbl -> GoToTimecode(This,timecode,error) ) 

#define IDeckLinkDeckControl_v7_9_FastForward(This,viewTape,error)	\
    ( (This)->lpVtbl -> FastForward(This,viewTape,error) ) 

#define IDeckLinkDeckControl_v7_9_Rewind(This,viewTape,error)	\
    ( (This)->lpVtbl -> Rewind(This,viewTape,error) ) 

#define IDeckLinkDeckControl_v7_9_StepForward(This,error)	\
    ( (This)->lpVtbl -> StepForward(This,error) ) 

#define IDeckLinkDeckControl_v7_9_StepBack(This,error)	\
    ( (This)->lpVtbl -> StepBack(This,error) ) 

#define IDeckLinkDeckControl_v7_9_Jog(This,rate,error)	\
    ( (This)->lpVtbl -> Jog(This,rate,error) ) 

#define IDeckLinkDeckControl_v7_9_Shuttle(This,rate,error)	\
    ( (This)->lpVtbl -> Shuttle(This,rate,error) ) 

#define IDeckLinkDeckControl_v7_9_GetTimecodeString(This,currentTimeCode,error)	\
    ( (This)->lpVtbl -> GetTimecodeString(This,currentTimeCode,error) ) 

#define IDeckLinkDeckControl_v7_9_GetTimecode(This,currentTimecode,error)	\
    ( (This)->lpVtbl -> GetTimecode(This,currentTimecode,error) ) 

#define IDeckLinkDeckControl_v7_9_GetTimecodeBCD(This,currentTimecode,error)	\
    ( (This)->lpVtbl -> GetTimecodeBCD(This,currentTimecode,error) ) 

#define IDeckLinkDeckControl_v7_9_SetPreroll(This,prerollSeconds)	\
    ( (This)->lpVtbl -> SetPreroll(This,prerollSeconds) ) 

#define IDeckLinkDeckControl_v7_9_GetPreroll(This,prerollSeconds)	\
    ( (This)->lpVtbl -> GetPreroll(This,prerollSeconds) ) 

#define IDeckLinkDeckControl_v7_9_SetExportOffset(This,exportOffsetFields)	\
    ( (This)->lpVtbl -> SetExportOffset(This,exportOffsetFields) ) 

#define IDeckLinkDeckControl_v7_9_GetExportOffset(This,exportOffsetFields)	\
    ( (This)->lpVtbl -> GetExportOffset(This,exportOffsetFields) ) 

#define IDeckLinkDeckControl_v7_9_GetManualExportOffset(This,deckManualExportOffsetFields)	\
    ( (This)->lpVtbl -> GetManualExportOffset(This,deckManualExportOffsetFields) ) 

#define IDeckLinkDeckControl_v7_9_SetCaptureOffset(This,captureOffsetFields)	\
    ( (This)->lpVtbl -> SetCaptureOffset(This,captureOffsetFields) ) 

#define IDeckLinkDeckControl_v7_9_GetCaptureOffset(This,captureOffsetFields)	\
    ( (This)->lpVtbl -> GetCaptureOffset(This,captureOffsetFields) ) 

#define IDeckLinkDeckControl_v7_9_StartExport(This,inTimecode,outTimecode,exportModeOps,error)	\
    ( (This)->lpVtbl -> StartExport(This,inTimecode,outTimecode,exportModeOps,error) ) 

#define IDeckLinkDeckControl_v7_9_StartCapture(This,useVITC,inTimecode,outTimecode,error)	\
    ( (This)->lpVtbl -> StartCapture(This,useVITC,inTimecode,outTimecode,error) ) 

#define IDeckLinkDeckControl_v7_9_GetDeviceID(This,deviceId,error)	\
    ( (This)->lpVtbl -> GetDeviceID(This,deviceId,error) ) 

#define IDeckLinkDeckControl_v7_9_Abort(This)	\
    ( (This)->lpVtbl -> Abort(This) ) 

#define IDeckLinkDeckControl_v7_9_CrashRecordStart(This,error)	\
    ( (This)->lpVtbl -> CrashRecordStart(This,error) ) 

#define IDeckLinkDeckControl_v7_9_CrashRecordStop(This,error)	\
    ( (This)->lpVtbl -> CrashRecordStop(This,error) ) 

#define IDeckLinkDeckControl_v7_9_SetCallback(This,callback)	\
    ( (This)->lpVtbl -> SetCallback(This,callback) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkDeckControl_v7_9_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkDisplayModeIterator_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkDisplayModeIterator_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkDisplayModeIterator_v7_6 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkDisplayModeIterator_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("455D741F-1779-4800-86F5-0B5D13D79751")
    IDeckLinkDisplayModeIterator_v7_6 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE Next( 
            /* [out] */ IDeckLinkDisplayMode_v7_6 **deckLinkDisplayMode) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkDisplayModeIterator_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkDisplayModeIterator_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkDisplayModeIterator_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkDisplayModeIterator_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *Next )( 
            IDeckLinkDisplayModeIterator_v7_6 * This,
            /* [out] */ IDeckLinkDisplayMode_v7_6 **deckLinkDisplayMode);
        
        END_INTERFACE
    } IDeckLinkDisplayModeIterator_v7_6Vtbl;

    interface IDeckLinkDisplayModeIterator_v7_6
    {
        CONST_VTBL struct IDeckLinkDisplayModeIterator_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkDisplayModeIterator_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkDisplayModeIterator_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkDisplayModeIterator_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkDisplayModeIterator_v7_6_Next(This,deckLinkDisplayMode)	\
    ( (This)->lpVtbl -> Next(This,deckLinkDisplayMode) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkDisplayModeIterator_v7_6_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkDisplayMode_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkDisplayMode_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkDisplayMode_v7_6 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkDisplayMode_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("87451E84-2B7E-439E-A629-4393EA4A8550")
    IDeckLinkDisplayMode_v7_6 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetName( 
            /* [out] */ BSTR *name) = 0;
        
        virtual BMDDisplayMode STDMETHODCALLTYPE GetDisplayMode( void) = 0;
        
        virtual long STDMETHODCALLTYPE GetWidth( void) = 0;
        
        virtual long STDMETHODCALLTYPE GetHeight( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetFrameRate( 
            /* [out] */ BMDTimeValue *frameDuration,
            /* [out] */ BMDTimeScale *timeScale) = 0;
        
        virtual BMDFieldDominance STDMETHODCALLTYPE GetFieldDominance( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkDisplayMode_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkDisplayMode_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkDisplayMode_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkDisplayMode_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetName )( 
            IDeckLinkDisplayMode_v7_6 * This,
            /* [out] */ BSTR *name);
        
        BMDDisplayMode ( STDMETHODCALLTYPE *GetDisplayMode )( 
            IDeckLinkDisplayMode_v7_6 * This);
        
        long ( STDMETHODCALLTYPE *GetWidth )( 
            IDeckLinkDisplayMode_v7_6 * This);
        
        long ( STDMETHODCALLTYPE *GetHeight )( 
            IDeckLinkDisplayMode_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetFrameRate )( 
            IDeckLinkDisplayMode_v7_6 * This,
            /* [out] */ BMDTimeValue *frameDuration,
            /* [out] */ BMDTimeScale *timeScale);
        
        BMDFieldDominance ( STDMETHODCALLTYPE *GetFieldDominance )( 
            IDeckLinkDisplayMode_v7_6 * This);
        
        END_INTERFACE
    } IDeckLinkDisplayMode_v7_6Vtbl;

    interface IDeckLinkDisplayMode_v7_6
    {
        CONST_VTBL struct IDeckLinkDisplayMode_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkDisplayMode_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkDisplayMode_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkDisplayMode_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkDisplayMode_v7_6_GetName(This,name)	\
    ( (This)->lpVtbl -> GetName(This,name) ) 

#define IDeckLinkDisplayMode_v7_6_GetDisplayMode(This)	\
    ( (This)->lpVtbl -> GetDisplayMode(This) ) 

#define IDeckLinkDisplayMode_v7_6_GetWidth(This)	\
    ( (This)->lpVtbl -> GetWidth(This) ) 

#define IDeckLinkDisplayMode_v7_6_GetHeight(This)	\
    ( (This)->lpVtbl -> GetHeight(This) ) 

#define IDeckLinkDisplayMode_v7_6_GetFrameRate(This,frameDuration,timeScale)	\
    ( (This)->lpVtbl -> GetFrameRate(This,frameDuration,timeScale) ) 

#define IDeckLinkDisplayMode_v7_6_GetFieldDominance(This)	\
    ( (This)->lpVtbl -> GetFieldDominance(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkDisplayMode_v7_6_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkOutput_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkOutput_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkOutput_v7_6 */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkOutput_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("29228142-EB8C-4141-A621-F74026450955")
    IDeckLinkOutput_v7_6 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE DoesSupportVideoMode( 
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            /* [out] */ BMDDisplayModeSupport *result) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetDisplayModeIterator( 
            /* [out] */ IDeckLinkDisplayModeIterator_v7_6 **iterator) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetScreenPreviewCallback( 
            /* [in] */ IDeckLinkScreenPreviewCallback_v7_6 *previewCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableVideoOutput( 
            BMDDisplayMode displayMode,
            BMDVideoOutputFlags flags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableVideoOutput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetVideoOutputFrameMemoryAllocator( 
            /* [in] */ IDeckLinkMemoryAllocator *theAllocator) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE CreateVideoFrame( 
            long width,
            long height,
            long rowBytes,
            BMDPixelFormat pixelFormat,
            BMDFrameFlags flags,
            /* [out] */ IDeckLinkMutableVideoFrame_v7_6 **outFrame) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE CreateAncillaryData( 
            BMDPixelFormat pixelFormat,
            /* [out] */ IDeckLinkVideoFrameAncillary **outBuffer) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisplayVideoFrameSync( 
            /* [in] */ IDeckLinkVideoFrame_v7_6 *theFrame) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ScheduleVideoFrame( 
            /* [in] */ IDeckLinkVideoFrame_v7_6 *theFrame,
            BMDTimeValue displayTime,
            BMDTimeValue displayDuration,
            BMDTimeScale timeScale) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetScheduledFrameCompletionCallback( 
            /* [in] */ IDeckLinkVideoOutputCallback_v7_6 *theCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBufferedVideoFrameCount( 
            /* [out] */ unsigned long *bufferedFrameCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableAudioOutput( 
            BMDAudioSampleRate sampleRate,
            BMDAudioSampleType sampleType,
            unsigned long channelCount,
            BMDAudioOutputStreamType streamType) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableAudioOutput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE WriteAudioSamplesSync( 
            /* [in] */ void *buffer,
            unsigned long sampleFrameCount,
            /* [out] */ unsigned long *sampleFramesWritten) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE BeginAudioPreroll( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EndAudioPreroll( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ScheduleAudioSamples( 
            /* [in] */ void *buffer,
            unsigned long sampleFrameCount,
            BMDTimeValue streamTime,
            BMDTimeScale timeScale,
            /* [out] */ unsigned long *sampleFramesWritten) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBufferedAudioSampleFrameCount( 
            /* [out] */ unsigned long *bufferedSampleFrameCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE FlushBufferedAudioSamples( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetAudioCallback( 
            /* [in] */ IDeckLinkAudioOutputCallback *theCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StartScheduledPlayback( 
            BMDTimeValue playbackStartTime,
            BMDTimeScale timeScale,
            double playbackSpeed) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StopScheduledPlayback( 
            BMDTimeValue stopPlaybackAtTime,
            /* [out] */ BMDTimeValue *actualStopTime,
            BMDTimeScale timeScale) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE IsScheduledPlaybackRunning( 
            /* [out] */ BOOL *active) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetScheduledStreamTime( 
            BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *streamTime,
            /* [out] */ double *playbackSpeed) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetHardwareReferenceClock( 
            BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *hardwareTime,
            /* [out] */ BMDTimeValue *timeInFrame,
            /* [out] */ BMDTimeValue *ticksPerFrame) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkOutput_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkOutput_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkOutput_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkOutput_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *DoesSupportVideoMode )( 
            IDeckLinkOutput_v7_6 * This,
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            /* [out] */ BMDDisplayModeSupport *result);
        
        HRESULT ( STDMETHODCALLTYPE *GetDisplayModeIterator )( 
            IDeckLinkOutput_v7_6 * This,
            /* [out] */ IDeckLinkDisplayModeIterator_v7_6 **iterator);
        
        HRESULT ( STDMETHODCALLTYPE *SetScreenPreviewCallback )( 
            IDeckLinkOutput_v7_6 * This,
            /* [in] */ IDeckLinkScreenPreviewCallback_v7_6 *previewCallback);
        
        HRESULT ( STDMETHODCALLTYPE *EnableVideoOutput )( 
            IDeckLinkOutput_v7_6 * This,
            BMDDisplayMode displayMode,
            BMDVideoOutputFlags flags);
        
        HRESULT ( STDMETHODCALLTYPE *DisableVideoOutput )( 
            IDeckLinkOutput_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetVideoOutputFrameMemoryAllocator )( 
            IDeckLinkOutput_v7_6 * This,
            /* [in] */ IDeckLinkMemoryAllocator *theAllocator);
        
        HRESULT ( STDMETHODCALLTYPE *CreateVideoFrame )( 
            IDeckLinkOutput_v7_6 * This,
            long width,
            long height,
            long rowBytes,
            BMDPixelFormat pixelFormat,
            BMDFrameFlags flags,
            /* [out] */ IDeckLinkMutableVideoFrame_v7_6 **outFrame);
        
        HRESULT ( STDMETHODCALLTYPE *CreateAncillaryData )( 
            IDeckLinkOutput_v7_6 * This,
            BMDPixelFormat pixelFormat,
            /* [out] */ IDeckLinkVideoFrameAncillary **outBuffer);
        
        HRESULT ( STDMETHODCALLTYPE *DisplayVideoFrameSync )( 
            IDeckLinkOutput_v7_6 * This,
            /* [in] */ IDeckLinkVideoFrame_v7_6 *theFrame);
        
        HRESULT ( STDMETHODCALLTYPE *ScheduleVideoFrame )( 
            IDeckLinkOutput_v7_6 * This,
            /* [in] */ IDeckLinkVideoFrame_v7_6 *theFrame,
            BMDTimeValue displayTime,
            BMDTimeValue displayDuration,
            BMDTimeScale timeScale);
        
        HRESULT ( STDMETHODCALLTYPE *SetScheduledFrameCompletionCallback )( 
            IDeckLinkOutput_v7_6 * This,
            /* [in] */ IDeckLinkVideoOutputCallback_v7_6 *theCallback);
        
        HRESULT ( STDMETHODCALLTYPE *GetBufferedVideoFrameCount )( 
            IDeckLinkOutput_v7_6 * This,
            /* [out] */ unsigned long *bufferedFrameCount);
        
        HRESULT ( STDMETHODCALLTYPE *EnableAudioOutput )( 
            IDeckLinkOutput_v7_6 * This,
            BMDAudioSampleRate sampleRate,
            BMDAudioSampleType sampleType,
            unsigned long channelCount,
            BMDAudioOutputStreamType streamType);
        
        HRESULT ( STDMETHODCALLTYPE *DisableAudioOutput )( 
            IDeckLinkOutput_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *WriteAudioSamplesSync )( 
            IDeckLinkOutput_v7_6 * This,
            /* [in] */ void *buffer,
            unsigned long sampleFrameCount,
            /* [out] */ unsigned long *sampleFramesWritten);
        
        HRESULT ( STDMETHODCALLTYPE *BeginAudioPreroll )( 
            IDeckLinkOutput_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *EndAudioPreroll )( 
            IDeckLinkOutput_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *ScheduleAudioSamples )( 
            IDeckLinkOutput_v7_6 * This,
            /* [in] */ void *buffer,
            unsigned long sampleFrameCount,
            BMDTimeValue streamTime,
            BMDTimeScale timeScale,
            /* [out] */ unsigned long *sampleFramesWritten);
        
        HRESULT ( STDMETHODCALLTYPE *GetBufferedAudioSampleFrameCount )( 
            IDeckLinkOutput_v7_6 * This,
            /* [out] */ unsigned long *bufferedSampleFrameCount);
        
        HRESULT ( STDMETHODCALLTYPE *FlushBufferedAudioSamples )( 
            IDeckLinkOutput_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetAudioCallback )( 
            IDeckLinkOutput_v7_6 * This,
            /* [in] */ IDeckLinkAudioOutputCallback *theCallback);
        
        HRESULT ( STDMETHODCALLTYPE *StartScheduledPlayback )( 
            IDeckLinkOutput_v7_6 * This,
            BMDTimeValue playbackStartTime,
            BMDTimeScale timeScale,
            double playbackSpeed);
        
        HRESULT ( STDMETHODCALLTYPE *StopScheduledPlayback )( 
            IDeckLinkOutput_v7_6 * This,
            BMDTimeValue stopPlaybackAtTime,
            /* [out] */ BMDTimeValue *actualStopTime,
            BMDTimeScale timeScale);
        
        HRESULT ( STDMETHODCALLTYPE *IsScheduledPlaybackRunning )( 
            IDeckLinkOutput_v7_6 * This,
            /* [out] */ BOOL *active);
        
        HRESULT ( STDMETHODCALLTYPE *GetScheduledStreamTime )( 
            IDeckLinkOutput_v7_6 * This,
            BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *streamTime,
            /* [out] */ double *playbackSpeed);
        
        HRESULT ( STDMETHODCALLTYPE *GetHardwareReferenceClock )( 
            IDeckLinkOutput_v7_6 * This,
            BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *hardwareTime,
            /* [out] */ BMDTimeValue *timeInFrame,
            /* [out] */ BMDTimeValue *ticksPerFrame);
        
        END_INTERFACE
    } IDeckLinkOutput_v7_6Vtbl;

    interface IDeckLinkOutput_v7_6
    {
        CONST_VTBL struct IDeckLinkOutput_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkOutput_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkOutput_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkOutput_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkOutput_v7_6_DoesSupportVideoMode(This,displayMode,pixelFormat,result)	\
    ( (This)->lpVtbl -> DoesSupportVideoMode(This,displayMode,pixelFormat,result) ) 

#define IDeckLinkOutput_v7_6_GetDisplayModeIterator(This,iterator)	\
    ( (This)->lpVtbl -> GetDisplayModeIterator(This,iterator) ) 

#define IDeckLinkOutput_v7_6_SetScreenPreviewCallback(This,previewCallback)	\
    ( (This)->lpVtbl -> SetScreenPreviewCallback(This,previewCallback) ) 

#define IDeckLinkOutput_v7_6_EnableVideoOutput(This,displayMode,flags)	\
    ( (This)->lpVtbl -> EnableVideoOutput(This,displayMode,flags) ) 

#define IDeckLinkOutput_v7_6_DisableVideoOutput(This)	\
    ( (This)->lpVtbl -> DisableVideoOutput(This) ) 

#define IDeckLinkOutput_v7_6_SetVideoOutputFrameMemoryAllocator(This,theAllocator)	\
    ( (This)->lpVtbl -> SetVideoOutputFrameMemoryAllocator(This,theAllocator) ) 

#define IDeckLinkOutput_v7_6_CreateVideoFrame(This,width,height,rowBytes,pixelFormat,flags,outFrame)	\
    ( (This)->lpVtbl -> CreateVideoFrame(This,width,height,rowBytes,pixelFormat,flags,outFrame) ) 

#define IDeckLinkOutput_v7_6_CreateAncillaryData(This,pixelFormat,outBuffer)	\
    ( (This)->lpVtbl -> CreateAncillaryData(This,pixelFormat,outBuffer) ) 

#define IDeckLinkOutput_v7_6_DisplayVideoFrameSync(This,theFrame)	\
    ( (This)->lpVtbl -> DisplayVideoFrameSync(This,theFrame) ) 

#define IDeckLinkOutput_v7_6_ScheduleVideoFrame(This,theFrame,displayTime,displayDuration,timeScale)	\
    ( (This)->lpVtbl -> ScheduleVideoFrame(This,theFrame,displayTime,displayDuration,timeScale) ) 

#define IDeckLinkOutput_v7_6_SetScheduledFrameCompletionCallback(This,theCallback)	\
    ( (This)->lpVtbl -> SetScheduledFrameCompletionCallback(This,theCallback) ) 

#define IDeckLinkOutput_v7_6_GetBufferedVideoFrameCount(This,bufferedFrameCount)	\
    ( (This)->lpVtbl -> GetBufferedVideoFrameCount(This,bufferedFrameCount) ) 

#define IDeckLinkOutput_v7_6_EnableAudioOutput(This,sampleRate,sampleType,channelCount,streamType)	\
    ( (This)->lpVtbl -> EnableAudioOutput(This,sampleRate,sampleType,channelCount,streamType) ) 

#define IDeckLinkOutput_v7_6_DisableAudioOutput(This)	\
    ( (This)->lpVtbl -> DisableAudioOutput(This) ) 

#define IDeckLinkOutput_v7_6_WriteAudioSamplesSync(This,buffer,sampleFrameCount,sampleFramesWritten)	\
    ( (This)->lpVtbl -> WriteAudioSamplesSync(This,buffer,sampleFrameCount,sampleFramesWritten) ) 

#define IDeckLinkOutput_v7_6_BeginAudioPreroll(This)	\
    ( (This)->lpVtbl -> BeginAudioPreroll(This) ) 

#define IDeckLinkOutput_v7_6_EndAudioPreroll(This)	\
    ( (This)->lpVtbl -> EndAudioPreroll(This) ) 

#define IDeckLinkOutput_v7_6_ScheduleAudioSamples(This,buffer,sampleFrameCount,streamTime,timeScale,sampleFramesWritten)	\
    ( (This)->lpVtbl -> ScheduleAudioSamples(This,buffer,sampleFrameCount,streamTime,timeScale,sampleFramesWritten) ) 

#define IDeckLinkOutput_v7_6_GetBufferedAudioSampleFrameCount(This,bufferedSampleFrameCount)	\
    ( (This)->lpVtbl -> GetBufferedAudioSampleFrameCount(This,bufferedSampleFrameCount) ) 

#define IDeckLinkOutput_v7_6_FlushBufferedAudioSamples(This)	\
    ( (This)->lpVtbl -> FlushBufferedAudioSamples(This) ) 

#define IDeckLinkOutput_v7_6_SetAudioCallback(This,theCallback)	\
    ( (This)->lpVtbl -> SetAudioCallback(This,theCallback) ) 

#define IDeckLinkOutput_v7_6_StartScheduledPlayback(This,playbackStartTime,timeScale,playbackSpeed)	\
    ( (This)->lpVtbl -> StartScheduledPlayback(This,playbackStartTime,timeScale,playbackSpeed) ) 

#define IDeckLinkOutput_v7_6_StopScheduledPlayback(This,stopPlaybackAtTime,actualStopTime,timeScale)	\
    ( (This)->lpVtbl -> StopScheduledPlayback(This,stopPlaybackAtTime,actualStopTime,timeScale) ) 

#define IDeckLinkOutput_v7_6_IsScheduledPlaybackRunning(This,active)	\
    ( (This)->lpVtbl -> IsScheduledPlaybackRunning(This,active) ) 

#define IDeckLinkOutput_v7_6_GetScheduledStreamTime(This,desiredTimeScale,streamTime,playbackSpeed)	\
    ( (This)->lpVtbl -> GetScheduledStreamTime(This,desiredTimeScale,streamTime,playbackSpeed) ) 

#define IDeckLinkOutput_v7_6_GetHardwareReferenceClock(This,desiredTimeScale,hardwareTime,timeInFrame,ticksPerFrame)	\
    ( (This)->lpVtbl -> GetHardwareReferenceClock(This,desiredTimeScale,hardwareTime,timeInFrame,ticksPerFrame) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkOutput_v7_6_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkInput_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkInput_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkInput_v7_6 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkInput_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("300C135A-9F43-48E2-9906-6D7911D93CF1")
    IDeckLinkInput_v7_6 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE DoesSupportVideoMode( 
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            /* [out] */ BMDDisplayModeSupport *result) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetDisplayModeIterator( 
            /* [out] */ IDeckLinkDisplayModeIterator_v7_6 **iterator) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetScreenPreviewCallback( 
            /* [in] */ IDeckLinkScreenPreviewCallback_v7_6 *previewCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableVideoInput( 
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            BMDVideoInputFlags flags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableVideoInput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetAvailableVideoFrameCount( 
            /* [out] */ unsigned long *availableFrameCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableAudioInput( 
            BMDAudioSampleRate sampleRate,
            BMDAudioSampleType sampleType,
            unsigned long channelCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableAudioInput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetAvailableAudioSampleFrameCount( 
            /* [out] */ unsigned long *availableSampleFrameCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StartStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StopStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE PauseStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE FlushStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetCallback( 
            /* [in] */ IDeckLinkInputCallback_v7_6 *theCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetHardwareReferenceClock( 
            BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *hardwareTime,
            /* [out] */ BMDTimeValue *timeInFrame,
            /* [out] */ BMDTimeValue *ticksPerFrame) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkInput_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkInput_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkInput_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkInput_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *DoesSupportVideoMode )( 
            IDeckLinkInput_v7_6 * This,
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            /* [out] */ BMDDisplayModeSupport *result);
        
        HRESULT ( STDMETHODCALLTYPE *GetDisplayModeIterator )( 
            IDeckLinkInput_v7_6 * This,
            /* [out] */ IDeckLinkDisplayModeIterator_v7_6 **iterator);
        
        HRESULT ( STDMETHODCALLTYPE *SetScreenPreviewCallback )( 
            IDeckLinkInput_v7_6 * This,
            /* [in] */ IDeckLinkScreenPreviewCallback_v7_6 *previewCallback);
        
        HRESULT ( STDMETHODCALLTYPE *EnableVideoInput )( 
            IDeckLinkInput_v7_6 * This,
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            BMDVideoInputFlags flags);
        
        HRESULT ( STDMETHODCALLTYPE *DisableVideoInput )( 
            IDeckLinkInput_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetAvailableVideoFrameCount )( 
            IDeckLinkInput_v7_6 * This,
            /* [out] */ unsigned long *availableFrameCount);
        
        HRESULT ( STDMETHODCALLTYPE *EnableAudioInput )( 
            IDeckLinkInput_v7_6 * This,
            BMDAudioSampleRate sampleRate,
            BMDAudioSampleType sampleType,
            unsigned long channelCount);
        
        HRESULT ( STDMETHODCALLTYPE *DisableAudioInput )( 
            IDeckLinkInput_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetAvailableAudioSampleFrameCount )( 
            IDeckLinkInput_v7_6 * This,
            /* [out] */ unsigned long *availableSampleFrameCount);
        
        HRESULT ( STDMETHODCALLTYPE *StartStreams )( 
            IDeckLinkInput_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *StopStreams )( 
            IDeckLinkInput_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *PauseStreams )( 
            IDeckLinkInput_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *FlushStreams )( 
            IDeckLinkInput_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetCallback )( 
            IDeckLinkInput_v7_6 * This,
            /* [in] */ IDeckLinkInputCallback_v7_6 *theCallback);
        
        HRESULT ( STDMETHODCALLTYPE *GetHardwareReferenceClock )( 
            IDeckLinkInput_v7_6 * This,
            BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *hardwareTime,
            /* [out] */ BMDTimeValue *timeInFrame,
            /* [out] */ BMDTimeValue *ticksPerFrame);
        
        END_INTERFACE
    } IDeckLinkInput_v7_6Vtbl;

    interface IDeckLinkInput_v7_6
    {
        CONST_VTBL struct IDeckLinkInput_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkInput_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkInput_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkInput_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkInput_v7_6_DoesSupportVideoMode(This,displayMode,pixelFormat,result)	\
    ( (This)->lpVtbl -> DoesSupportVideoMode(This,displayMode,pixelFormat,result) ) 

#define IDeckLinkInput_v7_6_GetDisplayModeIterator(This,iterator)	\
    ( (This)->lpVtbl -> GetDisplayModeIterator(This,iterator) ) 

#define IDeckLinkInput_v7_6_SetScreenPreviewCallback(This,previewCallback)	\
    ( (This)->lpVtbl -> SetScreenPreviewCallback(This,previewCallback) ) 

#define IDeckLinkInput_v7_6_EnableVideoInput(This,displayMode,pixelFormat,flags)	\
    ( (This)->lpVtbl -> EnableVideoInput(This,displayMode,pixelFormat,flags) ) 

#define IDeckLinkInput_v7_6_DisableVideoInput(This)	\
    ( (This)->lpVtbl -> DisableVideoInput(This) ) 

#define IDeckLinkInput_v7_6_GetAvailableVideoFrameCount(This,availableFrameCount)	\
    ( (This)->lpVtbl -> GetAvailableVideoFrameCount(This,availableFrameCount) ) 

#define IDeckLinkInput_v7_6_EnableAudioInput(This,sampleRate,sampleType,channelCount)	\
    ( (This)->lpVtbl -> EnableAudioInput(This,sampleRate,sampleType,channelCount) ) 

#define IDeckLinkInput_v7_6_DisableAudioInput(This)	\
    ( (This)->lpVtbl -> DisableAudioInput(This) ) 

#define IDeckLinkInput_v7_6_GetAvailableAudioSampleFrameCount(This,availableSampleFrameCount)	\
    ( (This)->lpVtbl -> GetAvailableAudioSampleFrameCount(This,availableSampleFrameCount) ) 

#define IDeckLinkInput_v7_6_StartStreams(This)	\
    ( (This)->lpVtbl -> StartStreams(This) ) 

#define IDeckLinkInput_v7_6_StopStreams(This)	\
    ( (This)->lpVtbl -> StopStreams(This) ) 

#define IDeckLinkInput_v7_6_PauseStreams(This)	\
    ( (This)->lpVtbl -> PauseStreams(This) ) 

#define IDeckLinkInput_v7_6_FlushStreams(This)	\
    ( (This)->lpVtbl -> FlushStreams(This) ) 

#define IDeckLinkInput_v7_6_SetCallback(This,theCallback)	\
    ( (This)->lpVtbl -> SetCallback(This,theCallback) ) 

#define IDeckLinkInput_v7_6_GetHardwareReferenceClock(This,desiredTimeScale,hardwareTime,timeInFrame,ticksPerFrame)	\
    ( (This)->lpVtbl -> GetHardwareReferenceClock(This,desiredTimeScale,hardwareTime,timeInFrame,ticksPerFrame) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkInput_v7_6_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkTimecode_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkTimecode_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkTimecode_v7_6 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkTimecode_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("EFB9BCA6-A521-44F7-BD69-2332F24D9EE6")
    IDeckLinkTimecode_v7_6 : public IUnknown
    {
    public:
        virtual BMDTimecodeBCD STDMETHODCALLTYPE GetBCD( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetComponents( 
            /* [out] */ unsigned char *hours,
            /* [out] */ unsigned char *minutes,
            /* [out] */ unsigned char *seconds,
            /* [out] */ unsigned char *frames) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetString( 
            /* [out] */ BSTR *timecode) = 0;
        
        virtual BMDTimecodeFlags STDMETHODCALLTYPE GetFlags( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkTimecode_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkTimecode_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkTimecode_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkTimecode_v7_6 * This);
        
        BMDTimecodeBCD ( STDMETHODCALLTYPE *GetBCD )( 
            IDeckLinkTimecode_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetComponents )( 
            IDeckLinkTimecode_v7_6 * This,
            /* [out] */ unsigned char *hours,
            /* [out] */ unsigned char *minutes,
            /* [out] */ unsigned char *seconds,
            /* [out] */ unsigned char *frames);
        
        HRESULT ( STDMETHODCALLTYPE *GetString )( 
            IDeckLinkTimecode_v7_6 * This,
            /* [out] */ BSTR *timecode);
        
        BMDTimecodeFlags ( STDMETHODCALLTYPE *GetFlags )( 
            IDeckLinkTimecode_v7_6 * This);
        
        END_INTERFACE
    } IDeckLinkTimecode_v7_6Vtbl;

    interface IDeckLinkTimecode_v7_6
    {
        CONST_VTBL struct IDeckLinkTimecode_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkTimecode_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkTimecode_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkTimecode_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkTimecode_v7_6_GetBCD(This)	\
    ( (This)->lpVtbl -> GetBCD(This) ) 

#define IDeckLinkTimecode_v7_6_GetComponents(This,hours,minutes,seconds,frames)	\
    ( (This)->lpVtbl -> GetComponents(This,hours,minutes,seconds,frames) ) 

#define IDeckLinkTimecode_v7_6_GetString(This,timecode)	\
    ( (This)->lpVtbl -> GetString(This,timecode) ) 

#define IDeckLinkTimecode_v7_6_GetFlags(This)	\
    ( (This)->lpVtbl -> GetFlags(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkTimecode_v7_6_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkVideoFrame_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkVideoFrame_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkVideoFrame_v7_6 */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoFrame_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("A8D8238E-6B18-4196-99E1-5AF717B83D32")
    IDeckLinkVideoFrame_v7_6 : public IUnknown
    {
    public:
        virtual long STDMETHODCALLTYPE GetWidth( void) = 0;
        
        virtual long STDMETHODCALLTYPE GetHeight( void) = 0;
        
        virtual long STDMETHODCALLTYPE GetRowBytes( void) = 0;
        
        virtual BMDPixelFormat STDMETHODCALLTYPE GetPixelFormat( void) = 0;
        
        virtual BMDFrameFlags STDMETHODCALLTYPE GetFlags( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBytes( 
            /* [out] */ void **buffer) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetTimecode( 
            BMDTimecodeFormat format,
            /* [out] */ IDeckLinkTimecode_v7_6 **timecode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetAncillaryData( 
            /* [out] */ IDeckLinkVideoFrameAncillary **ancillary) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoFrame_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoFrame_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoFrame_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoFrame_v7_6 * This);
        
        long ( STDMETHODCALLTYPE *GetWidth )( 
            IDeckLinkVideoFrame_v7_6 * This);
        
        long ( STDMETHODCALLTYPE *GetHeight )( 
            IDeckLinkVideoFrame_v7_6 * This);
        
        long ( STDMETHODCALLTYPE *GetRowBytes )( 
            IDeckLinkVideoFrame_v7_6 * This);
        
        BMDPixelFormat ( STDMETHODCALLTYPE *GetPixelFormat )( 
            IDeckLinkVideoFrame_v7_6 * This);
        
        BMDFrameFlags ( STDMETHODCALLTYPE *GetFlags )( 
            IDeckLinkVideoFrame_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetBytes )( 
            IDeckLinkVideoFrame_v7_6 * This,
            /* [out] */ void **buffer);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecode )( 
            IDeckLinkVideoFrame_v7_6 * This,
            BMDTimecodeFormat format,
            /* [out] */ IDeckLinkTimecode_v7_6 **timecode);
        
        HRESULT ( STDMETHODCALLTYPE *GetAncillaryData )( 
            IDeckLinkVideoFrame_v7_6 * This,
            /* [out] */ IDeckLinkVideoFrameAncillary **ancillary);
        
        END_INTERFACE
    } IDeckLinkVideoFrame_v7_6Vtbl;

    interface IDeckLinkVideoFrame_v7_6
    {
        CONST_VTBL struct IDeckLinkVideoFrame_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoFrame_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoFrame_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoFrame_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoFrame_v7_6_GetWidth(This)	\
    ( (This)->lpVtbl -> GetWidth(This) ) 

#define IDeckLinkVideoFrame_v7_6_GetHeight(This)	\
    ( (This)->lpVtbl -> GetHeight(This) ) 

#define IDeckLinkVideoFrame_v7_6_GetRowBytes(This)	\
    ( (This)->lpVtbl -> GetRowBytes(This) ) 

#define IDeckLinkVideoFrame_v7_6_GetPixelFormat(This)	\
    ( (This)->lpVtbl -> GetPixelFormat(This) ) 

#define IDeckLinkVideoFrame_v7_6_GetFlags(This)	\
    ( (This)->lpVtbl -> GetFlags(This) ) 

#define IDeckLinkVideoFrame_v7_6_GetBytes(This,buffer)	\
    ( (This)->lpVtbl -> GetBytes(This,buffer) ) 

#define IDeckLinkVideoFrame_v7_6_GetTimecode(This,format,timecode)	\
    ( (This)->lpVtbl -> GetTimecode(This,format,timecode) ) 

#define IDeckLinkVideoFrame_v7_6_GetAncillaryData(This,ancillary)	\
    ( (This)->lpVtbl -> GetAncillaryData(This,ancillary) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoFrame_v7_6_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkMutableVideoFrame_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkMutableVideoFrame_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkMutableVideoFrame_v7_6 */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkMutableVideoFrame_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("46FCEE00-B4E6-43D0-91C0-023A7FCEB34F")
    IDeckLinkMutableVideoFrame_v7_6 : public IDeckLinkVideoFrame_v7_6
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE SetFlags( 
            BMDFrameFlags newFlags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetTimecode( 
            BMDTimecodeFormat format,
            /* [in] */ IDeckLinkTimecode_v7_6 *timecode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetTimecodeFromComponents( 
            BMDTimecodeFormat format,
            unsigned char hours,
            unsigned char minutes,
            unsigned char seconds,
            unsigned char frames,
            BMDTimecodeFlags flags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetAncillaryData( 
            /* [in] */ IDeckLinkVideoFrameAncillary *ancillary) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkMutableVideoFrame_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkMutableVideoFrame_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkMutableVideoFrame_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkMutableVideoFrame_v7_6 * This);
        
        long ( STDMETHODCALLTYPE *GetWidth )( 
            IDeckLinkMutableVideoFrame_v7_6 * This);
        
        long ( STDMETHODCALLTYPE *GetHeight )( 
            IDeckLinkMutableVideoFrame_v7_6 * This);
        
        long ( STDMETHODCALLTYPE *GetRowBytes )( 
            IDeckLinkMutableVideoFrame_v7_6 * This);
        
        BMDPixelFormat ( STDMETHODCALLTYPE *GetPixelFormat )( 
            IDeckLinkMutableVideoFrame_v7_6 * This);
        
        BMDFrameFlags ( STDMETHODCALLTYPE *GetFlags )( 
            IDeckLinkMutableVideoFrame_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetBytes )( 
            IDeckLinkMutableVideoFrame_v7_6 * This,
            /* [out] */ void **buffer);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecode )( 
            IDeckLinkMutableVideoFrame_v7_6 * This,
            BMDTimecodeFormat format,
            /* [out] */ IDeckLinkTimecode_v7_6 **timecode);
        
        HRESULT ( STDMETHODCALLTYPE *GetAncillaryData )( 
            IDeckLinkMutableVideoFrame_v7_6 * This,
            /* [out] */ IDeckLinkVideoFrameAncillary **ancillary);
        
        HRESULT ( STDMETHODCALLTYPE *SetFlags )( 
            IDeckLinkMutableVideoFrame_v7_6 * This,
            BMDFrameFlags newFlags);
        
        HRESULT ( STDMETHODCALLTYPE *SetTimecode )( 
            IDeckLinkMutableVideoFrame_v7_6 * This,
            BMDTimecodeFormat format,
            /* [in] */ IDeckLinkTimecode_v7_6 *timecode);
        
        HRESULT ( STDMETHODCALLTYPE *SetTimecodeFromComponents )( 
            IDeckLinkMutableVideoFrame_v7_6 * This,
            BMDTimecodeFormat format,
            unsigned char hours,
            unsigned char minutes,
            unsigned char seconds,
            unsigned char frames,
            BMDTimecodeFlags flags);
        
        HRESULT ( STDMETHODCALLTYPE *SetAncillaryData )( 
            IDeckLinkMutableVideoFrame_v7_6 * This,
            /* [in] */ IDeckLinkVideoFrameAncillary *ancillary);
        
        END_INTERFACE
    } IDeckLinkMutableVideoFrame_v7_6Vtbl;

    interface IDeckLinkMutableVideoFrame_v7_6
    {
        CONST_VTBL struct IDeckLinkMutableVideoFrame_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkMutableVideoFrame_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkMutableVideoFrame_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkMutableVideoFrame_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkMutableVideoFrame_v7_6_GetWidth(This)	\
    ( (This)->lpVtbl -> GetWidth(This) ) 

#define IDeckLinkMutableVideoFrame_v7_6_GetHeight(This)	\
    ( (This)->lpVtbl -> GetHeight(This) ) 

#define IDeckLinkMutableVideoFrame_v7_6_GetRowBytes(This)	\
    ( (This)->lpVtbl -> GetRowBytes(This) ) 

#define IDeckLinkMutableVideoFrame_v7_6_GetPixelFormat(This)	\
    ( (This)->lpVtbl -> GetPixelFormat(This) ) 

#define IDeckLinkMutableVideoFrame_v7_6_GetFlags(This)	\
    ( (This)->lpVtbl -> GetFlags(This) ) 

#define IDeckLinkMutableVideoFrame_v7_6_GetBytes(This,buffer)	\
    ( (This)->lpVtbl -> GetBytes(This,buffer) ) 

#define IDeckLinkMutableVideoFrame_v7_6_GetTimecode(This,format,timecode)	\
    ( (This)->lpVtbl -> GetTimecode(This,format,timecode) ) 

#define IDeckLinkMutableVideoFrame_v7_6_GetAncillaryData(This,ancillary)	\
    ( (This)->lpVtbl -> GetAncillaryData(This,ancillary) ) 


#define IDeckLinkMutableVideoFrame_v7_6_SetFlags(This,newFlags)	\
    ( (This)->lpVtbl -> SetFlags(This,newFlags) ) 

#define IDeckLinkMutableVideoFrame_v7_6_SetTimecode(This,format,timecode)	\
    ( (This)->lpVtbl -> SetTimecode(This,format,timecode) ) 

#define IDeckLinkMutableVideoFrame_v7_6_SetTimecodeFromComponents(This,format,hours,minutes,seconds,frames,flags)	\
    ( (This)->lpVtbl -> SetTimecodeFromComponents(This,format,hours,minutes,seconds,frames,flags) ) 

#define IDeckLinkMutableVideoFrame_v7_6_SetAncillaryData(This,ancillary)	\
    ( (This)->lpVtbl -> SetAncillaryData(This,ancillary) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkMutableVideoFrame_v7_6_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkVideoInputFrame_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkVideoInputFrame_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkVideoInputFrame_v7_6 */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoInputFrame_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("9A74FA41-AE9F-47AC-8CF4-01F42DD59965")
    IDeckLinkVideoInputFrame_v7_6 : public IDeckLinkVideoFrame_v7_6
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetStreamTime( 
            /* [out] */ BMDTimeValue *frameTime,
            /* [out] */ BMDTimeValue *frameDuration,
            BMDTimeScale timeScale) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetHardwareReferenceTimestamp( 
            BMDTimeScale timeScale,
            /* [out] */ BMDTimeValue *frameTime,
            /* [out] */ BMDTimeValue *frameDuration) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoInputFrame_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoInputFrame_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoInputFrame_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoInputFrame_v7_6 * This);
        
        long ( STDMETHODCALLTYPE *GetWidth )( 
            IDeckLinkVideoInputFrame_v7_6 * This);
        
        long ( STDMETHODCALLTYPE *GetHeight )( 
            IDeckLinkVideoInputFrame_v7_6 * This);
        
        long ( STDMETHODCALLTYPE *GetRowBytes )( 
            IDeckLinkVideoInputFrame_v7_6 * This);
        
        BMDPixelFormat ( STDMETHODCALLTYPE *GetPixelFormat )( 
            IDeckLinkVideoInputFrame_v7_6 * This);
        
        BMDFrameFlags ( STDMETHODCALLTYPE *GetFlags )( 
            IDeckLinkVideoInputFrame_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetBytes )( 
            IDeckLinkVideoInputFrame_v7_6 * This,
            /* [out] */ void **buffer);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecode )( 
            IDeckLinkVideoInputFrame_v7_6 * This,
            BMDTimecodeFormat format,
            /* [out] */ IDeckLinkTimecode_v7_6 **timecode);
        
        HRESULT ( STDMETHODCALLTYPE *GetAncillaryData )( 
            IDeckLinkVideoInputFrame_v7_6 * This,
            /* [out] */ IDeckLinkVideoFrameAncillary **ancillary);
        
        HRESULT ( STDMETHODCALLTYPE *GetStreamTime )( 
            IDeckLinkVideoInputFrame_v7_6 * This,
            /* [out] */ BMDTimeValue *frameTime,
            /* [out] */ BMDTimeValue *frameDuration,
            BMDTimeScale timeScale);
        
        HRESULT ( STDMETHODCALLTYPE *GetHardwareReferenceTimestamp )( 
            IDeckLinkVideoInputFrame_v7_6 * This,
            BMDTimeScale timeScale,
            /* [out] */ BMDTimeValue *frameTime,
            /* [out] */ BMDTimeValue *frameDuration);
        
        END_INTERFACE
    } IDeckLinkVideoInputFrame_v7_6Vtbl;

    interface IDeckLinkVideoInputFrame_v7_6
    {
        CONST_VTBL struct IDeckLinkVideoInputFrame_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoInputFrame_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoInputFrame_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoInputFrame_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoInputFrame_v7_6_GetWidth(This)	\
    ( (This)->lpVtbl -> GetWidth(This) ) 

#define IDeckLinkVideoInputFrame_v7_6_GetHeight(This)	\
    ( (This)->lpVtbl -> GetHeight(This) ) 

#define IDeckLinkVideoInputFrame_v7_6_GetRowBytes(This)	\
    ( (This)->lpVtbl -> GetRowBytes(This) ) 

#define IDeckLinkVideoInputFrame_v7_6_GetPixelFormat(This)	\
    ( (This)->lpVtbl -> GetPixelFormat(This) ) 

#define IDeckLinkVideoInputFrame_v7_6_GetFlags(This)	\
    ( (This)->lpVtbl -> GetFlags(This) ) 

#define IDeckLinkVideoInputFrame_v7_6_GetBytes(This,buffer)	\
    ( (This)->lpVtbl -> GetBytes(This,buffer) ) 

#define IDeckLinkVideoInputFrame_v7_6_GetTimecode(This,format,timecode)	\
    ( (This)->lpVtbl -> GetTimecode(This,format,timecode) ) 

#define IDeckLinkVideoInputFrame_v7_6_GetAncillaryData(This,ancillary)	\
    ( (This)->lpVtbl -> GetAncillaryData(This,ancillary) ) 


#define IDeckLinkVideoInputFrame_v7_6_GetStreamTime(This,frameTime,frameDuration,timeScale)	\
    ( (This)->lpVtbl -> GetStreamTime(This,frameTime,frameDuration,timeScale) ) 

#define IDeckLinkVideoInputFrame_v7_6_GetHardwareReferenceTimestamp(This,timeScale,frameTime,frameDuration)	\
    ( (This)->lpVtbl -> GetHardwareReferenceTimestamp(This,timeScale,frameTime,frameDuration) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoInputFrame_v7_6_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkScreenPreviewCallback_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkScreenPreviewCallback_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkScreenPreviewCallback_v7_6 */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkScreenPreviewCallback_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("373F499D-4B4D-4518-AD22-6354E5A5825E")
    IDeckLinkScreenPreviewCallback_v7_6 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE DrawFrame( 
            /* [in] */ IDeckLinkVideoFrame_v7_6 *theFrame) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkScreenPreviewCallback_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkScreenPreviewCallback_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkScreenPreviewCallback_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkScreenPreviewCallback_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *DrawFrame )( 
            IDeckLinkScreenPreviewCallback_v7_6 * This,
            /* [in] */ IDeckLinkVideoFrame_v7_6 *theFrame);
        
        END_INTERFACE
    } IDeckLinkScreenPreviewCallback_v7_6Vtbl;

    interface IDeckLinkScreenPreviewCallback_v7_6
    {
        CONST_VTBL struct IDeckLinkScreenPreviewCallback_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkScreenPreviewCallback_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkScreenPreviewCallback_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkScreenPreviewCallback_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkScreenPreviewCallback_v7_6_DrawFrame(This,theFrame)	\
    ( (This)->lpVtbl -> DrawFrame(This,theFrame) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkScreenPreviewCallback_v7_6_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkGLScreenPreviewHelper_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkGLScreenPreviewHelper_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkGLScreenPreviewHelper_v7_6 */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkGLScreenPreviewHelper_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("BA575CD9-A15E-497B-B2C2-F9AFE7BE4EBA")
    IDeckLinkGLScreenPreviewHelper_v7_6 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE InitializeGL( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE PaintGL( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetFrame( 
            /* [in] */ IDeckLinkVideoFrame_v7_6 *theFrame) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkGLScreenPreviewHelper_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkGLScreenPreviewHelper_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkGLScreenPreviewHelper_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkGLScreenPreviewHelper_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *InitializeGL )( 
            IDeckLinkGLScreenPreviewHelper_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *PaintGL )( 
            IDeckLinkGLScreenPreviewHelper_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetFrame )( 
            IDeckLinkGLScreenPreviewHelper_v7_6 * This,
            /* [in] */ IDeckLinkVideoFrame_v7_6 *theFrame);
        
        END_INTERFACE
    } IDeckLinkGLScreenPreviewHelper_v7_6Vtbl;

    interface IDeckLinkGLScreenPreviewHelper_v7_6
    {
        CONST_VTBL struct IDeckLinkGLScreenPreviewHelper_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkGLScreenPreviewHelper_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkGLScreenPreviewHelper_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkGLScreenPreviewHelper_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkGLScreenPreviewHelper_v7_6_InitializeGL(This)	\
    ( (This)->lpVtbl -> InitializeGL(This) ) 

#define IDeckLinkGLScreenPreviewHelper_v7_6_PaintGL(This)	\
    ( (This)->lpVtbl -> PaintGL(This) ) 

#define IDeckLinkGLScreenPreviewHelper_v7_6_SetFrame(This,theFrame)	\
    ( (This)->lpVtbl -> SetFrame(This,theFrame) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkGLScreenPreviewHelper_v7_6_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkVideoConversion_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkVideoConversion_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkVideoConversion_v7_6 */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoConversion_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("3EB504C9-F97D-40FE-A158-D407D48CB53B")
    IDeckLinkVideoConversion_v7_6 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE ConvertFrame( 
            /* [in] */ IDeckLinkVideoFrame_v7_6 *srcFrame,
            /* [in] */ IDeckLinkVideoFrame_v7_6 *dstFrame) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoConversion_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoConversion_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoConversion_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoConversion_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *ConvertFrame )( 
            IDeckLinkVideoConversion_v7_6 * This,
            /* [in] */ IDeckLinkVideoFrame_v7_6 *srcFrame,
            /* [in] */ IDeckLinkVideoFrame_v7_6 *dstFrame);
        
        END_INTERFACE
    } IDeckLinkVideoConversion_v7_6Vtbl;

    interface IDeckLinkVideoConversion_v7_6
    {
        CONST_VTBL struct IDeckLinkVideoConversion_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoConversion_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoConversion_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoConversion_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoConversion_v7_6_ConvertFrame(This,srcFrame,dstFrame)	\
    ( (This)->lpVtbl -> ConvertFrame(This,srcFrame,dstFrame) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoConversion_v7_6_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkConfiguration_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkConfiguration_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkConfiguration_v7_6 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkConfiguration_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("B8EAD569-B764-47F0-A73F-AE40DF6CBF10")
    IDeckLinkConfiguration_v7_6 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetConfigurationValidator( 
            /* [out] */ IDeckLinkConfiguration_v7_6 **configObject) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE WriteConfigurationToPreferences( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetVideoOutputFormat( 
            /* [in] */ BMDVideoConnection_v7_6 videoOutputConnection) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE IsVideoOutputActive( 
            /* [in] */ BMDVideoConnection_v7_6 videoOutputConnection,
            /* [out] */ BOOL *active) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetAnalogVideoOutputFlags( 
            /* [in] */ BMDAnalogVideoFlags analogVideoFlags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetAnalogVideoOutputFlags( 
            /* [out] */ BMDAnalogVideoFlags *analogVideoFlags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableFieldFlickerRemovalWhenPaused( 
            /* [in] */ BOOL enable) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE IsEnabledFieldFlickerRemovalWhenPaused( 
            /* [out] */ BOOL *enabled) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Set444And3GBpsVideoOutput( 
            /* [in] */ BOOL enable444VideoOutput,
            /* [in] */ BOOL enable3GbsOutput) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Get444And3GBpsVideoOutput( 
            /* [out] */ BOOL *is444VideoOutputEnabled,
            /* [out] */ BOOL *threeGbsOutputEnabled) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetVideoOutputConversionMode( 
            /* [in] */ BMDVideoOutputConversionMode conversionMode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetVideoOutputConversionMode( 
            /* [out] */ BMDVideoOutputConversionMode *conversionMode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Set_HD1080p24_to_HD1080i5994_Conversion( 
            /* [in] */ BOOL enable) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Get_HD1080p24_to_HD1080i5994_Conversion( 
            /* [out] */ BOOL *enabled) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetVideoInputFormat( 
            /* [in] */ BMDVideoConnection_v7_6 videoInputFormat) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetVideoInputFormat( 
            /* [out] */ BMDVideoConnection_v7_6 *videoInputFormat) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetAnalogVideoInputFlags( 
            /* [in] */ BMDAnalogVideoFlags analogVideoFlags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetAnalogVideoInputFlags( 
            /* [out] */ BMDAnalogVideoFlags *analogVideoFlags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetVideoInputConversionMode( 
            /* [in] */ BMDVideoInputConversionMode conversionMode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetVideoInputConversionMode( 
            /* [out] */ BMDVideoInputConversionMode *conversionMode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetBlackVideoOutputDuringCapture( 
            /* [in] */ BOOL blackOutInCapture) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBlackVideoOutputDuringCapture( 
            /* [out] */ BOOL *blackOutInCapture) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Set32PulldownSequenceInitialTimecodeFrame( 
            /* [in] */ unsigned long aFrameTimecode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE Get32PulldownSequenceInitialTimecodeFrame( 
            /* [out] */ unsigned long *aFrameTimecode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetVancSourceLineMapping( 
            /* [in] */ unsigned long activeLine1VANCsource,
            /* [in] */ unsigned long activeLine2VANCsource,
            /* [in] */ unsigned long activeLine3VANCsource) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetVancSourceLineMapping( 
            /* [out] */ unsigned long *activeLine1VANCsource,
            /* [out] */ unsigned long *activeLine2VANCsource,
            /* [out] */ unsigned long *activeLine3VANCsource) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetAudioInputFormat( 
            /* [in] */ BMDAudioConnection audioInputFormat) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetAudioInputFormat( 
            /* [out] */ BMDAudioConnection *audioInputFormat) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkConfiguration_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkConfiguration_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkConfiguration_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetConfigurationValidator )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [out] */ IDeckLinkConfiguration_v7_6 **configObject);
        
        HRESULT ( STDMETHODCALLTYPE *WriteConfigurationToPreferences )( 
            IDeckLinkConfiguration_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetVideoOutputFormat )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ BMDVideoConnection_v7_6 videoOutputConnection);
        
        HRESULT ( STDMETHODCALLTYPE *IsVideoOutputActive )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ BMDVideoConnection_v7_6 videoOutputConnection,
            /* [out] */ BOOL *active);
        
        HRESULT ( STDMETHODCALLTYPE *SetAnalogVideoOutputFlags )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ BMDAnalogVideoFlags analogVideoFlags);
        
        HRESULT ( STDMETHODCALLTYPE *GetAnalogVideoOutputFlags )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [out] */ BMDAnalogVideoFlags *analogVideoFlags);
        
        HRESULT ( STDMETHODCALLTYPE *EnableFieldFlickerRemovalWhenPaused )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ BOOL enable);
        
        HRESULT ( STDMETHODCALLTYPE *IsEnabledFieldFlickerRemovalWhenPaused )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [out] */ BOOL *enabled);
        
        HRESULT ( STDMETHODCALLTYPE *Set444And3GBpsVideoOutput )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ BOOL enable444VideoOutput,
            /* [in] */ BOOL enable3GbsOutput);
        
        HRESULT ( STDMETHODCALLTYPE *Get444And3GBpsVideoOutput )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [out] */ BOOL *is444VideoOutputEnabled,
            /* [out] */ BOOL *threeGbsOutputEnabled);
        
        HRESULT ( STDMETHODCALLTYPE *SetVideoOutputConversionMode )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ BMDVideoOutputConversionMode conversionMode);
        
        HRESULT ( STDMETHODCALLTYPE *GetVideoOutputConversionMode )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [out] */ BMDVideoOutputConversionMode *conversionMode);
        
        HRESULT ( STDMETHODCALLTYPE *Set_HD1080p24_to_HD1080i5994_Conversion )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ BOOL enable);
        
        HRESULT ( STDMETHODCALLTYPE *Get_HD1080p24_to_HD1080i5994_Conversion )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [out] */ BOOL *enabled);
        
        HRESULT ( STDMETHODCALLTYPE *SetVideoInputFormat )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ BMDVideoConnection_v7_6 videoInputFormat);
        
        HRESULT ( STDMETHODCALLTYPE *GetVideoInputFormat )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [out] */ BMDVideoConnection_v7_6 *videoInputFormat);
        
        HRESULT ( STDMETHODCALLTYPE *SetAnalogVideoInputFlags )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ BMDAnalogVideoFlags analogVideoFlags);
        
        HRESULT ( STDMETHODCALLTYPE *GetAnalogVideoInputFlags )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [out] */ BMDAnalogVideoFlags *analogVideoFlags);
        
        HRESULT ( STDMETHODCALLTYPE *SetVideoInputConversionMode )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ BMDVideoInputConversionMode conversionMode);
        
        HRESULT ( STDMETHODCALLTYPE *GetVideoInputConversionMode )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [out] */ BMDVideoInputConversionMode *conversionMode);
        
        HRESULT ( STDMETHODCALLTYPE *SetBlackVideoOutputDuringCapture )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ BOOL blackOutInCapture);
        
        HRESULT ( STDMETHODCALLTYPE *GetBlackVideoOutputDuringCapture )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [out] */ BOOL *blackOutInCapture);
        
        HRESULT ( STDMETHODCALLTYPE *Set32PulldownSequenceInitialTimecodeFrame )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ unsigned long aFrameTimecode);
        
        HRESULT ( STDMETHODCALLTYPE *Get32PulldownSequenceInitialTimecodeFrame )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [out] */ unsigned long *aFrameTimecode);
        
        HRESULT ( STDMETHODCALLTYPE *SetVancSourceLineMapping )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ unsigned long activeLine1VANCsource,
            /* [in] */ unsigned long activeLine2VANCsource,
            /* [in] */ unsigned long activeLine3VANCsource);
        
        HRESULT ( STDMETHODCALLTYPE *GetVancSourceLineMapping )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [out] */ unsigned long *activeLine1VANCsource,
            /* [out] */ unsigned long *activeLine2VANCsource,
            /* [out] */ unsigned long *activeLine3VANCsource);
        
        HRESULT ( STDMETHODCALLTYPE *SetAudioInputFormat )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [in] */ BMDAudioConnection audioInputFormat);
        
        HRESULT ( STDMETHODCALLTYPE *GetAudioInputFormat )( 
            IDeckLinkConfiguration_v7_6 * This,
            /* [out] */ BMDAudioConnection *audioInputFormat);
        
        END_INTERFACE
    } IDeckLinkConfiguration_v7_6Vtbl;

    interface IDeckLinkConfiguration_v7_6
    {
        CONST_VTBL struct IDeckLinkConfiguration_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkConfiguration_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkConfiguration_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkConfiguration_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkConfiguration_v7_6_GetConfigurationValidator(This,configObject)	\
    ( (This)->lpVtbl -> GetConfigurationValidator(This,configObject) ) 

#define IDeckLinkConfiguration_v7_6_WriteConfigurationToPreferences(This)	\
    ( (This)->lpVtbl -> WriteConfigurationToPreferences(This) ) 

#define IDeckLinkConfiguration_v7_6_SetVideoOutputFormat(This,videoOutputConnection)	\
    ( (This)->lpVtbl -> SetVideoOutputFormat(This,videoOutputConnection) ) 

#define IDeckLinkConfiguration_v7_6_IsVideoOutputActive(This,videoOutputConnection,active)	\
    ( (This)->lpVtbl -> IsVideoOutputActive(This,videoOutputConnection,active) ) 

#define IDeckLinkConfiguration_v7_6_SetAnalogVideoOutputFlags(This,analogVideoFlags)	\
    ( (This)->lpVtbl -> SetAnalogVideoOutputFlags(This,analogVideoFlags) ) 

#define IDeckLinkConfiguration_v7_6_GetAnalogVideoOutputFlags(This,analogVideoFlags)	\
    ( (This)->lpVtbl -> GetAnalogVideoOutputFlags(This,analogVideoFlags) ) 

#define IDeckLinkConfiguration_v7_6_EnableFieldFlickerRemovalWhenPaused(This,enable)	\
    ( (This)->lpVtbl -> EnableFieldFlickerRemovalWhenPaused(This,enable) ) 

#define IDeckLinkConfiguration_v7_6_IsEnabledFieldFlickerRemovalWhenPaused(This,enabled)	\
    ( (This)->lpVtbl -> IsEnabledFieldFlickerRemovalWhenPaused(This,enabled) ) 

#define IDeckLinkConfiguration_v7_6_Set444And3GBpsVideoOutput(This,enable444VideoOutput,enable3GbsOutput)	\
    ( (This)->lpVtbl -> Set444And3GBpsVideoOutput(This,enable444VideoOutput,enable3GbsOutput) ) 

#define IDeckLinkConfiguration_v7_6_Get444And3GBpsVideoOutput(This,is444VideoOutputEnabled,threeGbsOutputEnabled)	\
    ( (This)->lpVtbl -> Get444And3GBpsVideoOutput(This,is444VideoOutputEnabled,threeGbsOutputEnabled) ) 

#define IDeckLinkConfiguration_v7_6_SetVideoOutputConversionMode(This,conversionMode)	\
    ( (This)->lpVtbl -> SetVideoOutputConversionMode(This,conversionMode) ) 

#define IDeckLinkConfiguration_v7_6_GetVideoOutputConversionMode(This,conversionMode)	\
    ( (This)->lpVtbl -> GetVideoOutputConversionMode(This,conversionMode) ) 

#define IDeckLinkConfiguration_v7_6_Set_HD1080p24_to_HD1080i5994_Conversion(This,enable)	\
    ( (This)->lpVtbl -> Set_HD1080p24_to_HD1080i5994_Conversion(This,enable) ) 

#define IDeckLinkConfiguration_v7_6_Get_HD1080p24_to_HD1080i5994_Conversion(This,enabled)	\
    ( (This)->lpVtbl -> Get_HD1080p24_to_HD1080i5994_Conversion(This,enabled) ) 

#define IDeckLinkConfiguration_v7_6_SetVideoInputFormat(This,videoInputFormat)	\
    ( (This)->lpVtbl -> SetVideoInputFormat(This,videoInputFormat) ) 

#define IDeckLinkConfiguration_v7_6_GetVideoInputFormat(This,videoInputFormat)	\
    ( (This)->lpVtbl -> GetVideoInputFormat(This,videoInputFormat) ) 

#define IDeckLinkConfiguration_v7_6_SetAnalogVideoInputFlags(This,analogVideoFlags)	\
    ( (This)->lpVtbl -> SetAnalogVideoInputFlags(This,analogVideoFlags) ) 

#define IDeckLinkConfiguration_v7_6_GetAnalogVideoInputFlags(This,analogVideoFlags)	\
    ( (This)->lpVtbl -> GetAnalogVideoInputFlags(This,analogVideoFlags) ) 

#define IDeckLinkConfiguration_v7_6_SetVideoInputConversionMode(This,conversionMode)	\
    ( (This)->lpVtbl -> SetVideoInputConversionMode(This,conversionMode) ) 

#define IDeckLinkConfiguration_v7_6_GetVideoInputConversionMode(This,conversionMode)	\
    ( (This)->lpVtbl -> GetVideoInputConversionMode(This,conversionMode) ) 

#define IDeckLinkConfiguration_v7_6_SetBlackVideoOutputDuringCapture(This,blackOutInCapture)	\
    ( (This)->lpVtbl -> SetBlackVideoOutputDuringCapture(This,blackOutInCapture) ) 

#define IDeckLinkConfiguration_v7_6_GetBlackVideoOutputDuringCapture(This,blackOutInCapture)	\
    ( (This)->lpVtbl -> GetBlackVideoOutputDuringCapture(This,blackOutInCapture) ) 

#define IDeckLinkConfiguration_v7_6_Set32PulldownSequenceInitialTimecodeFrame(This,aFrameTimecode)	\
    ( (This)->lpVtbl -> Set32PulldownSequenceInitialTimecodeFrame(This,aFrameTimecode) ) 

#define IDeckLinkConfiguration_v7_6_Get32PulldownSequenceInitialTimecodeFrame(This,aFrameTimecode)	\
    ( (This)->lpVtbl -> Get32PulldownSequenceInitialTimecodeFrame(This,aFrameTimecode) ) 

#define IDeckLinkConfiguration_v7_6_SetVancSourceLineMapping(This,activeLine1VANCsource,activeLine2VANCsource,activeLine3VANCsource)	\
    ( (This)->lpVtbl -> SetVancSourceLineMapping(This,activeLine1VANCsource,activeLine2VANCsource,activeLine3VANCsource) ) 

#define IDeckLinkConfiguration_v7_6_GetVancSourceLineMapping(This,activeLine1VANCsource,activeLine2VANCsource,activeLine3VANCsource)	\
    ( (This)->lpVtbl -> GetVancSourceLineMapping(This,activeLine1VANCsource,activeLine2VANCsource,activeLine3VANCsource) ) 

#define IDeckLinkConfiguration_v7_6_SetAudioInputFormat(This,audioInputFormat)	\
    ( (This)->lpVtbl -> SetAudioInputFormat(This,audioInputFormat) ) 

#define IDeckLinkConfiguration_v7_6_GetAudioInputFormat(This,audioInputFormat)	\
    ( (This)->lpVtbl -> GetAudioInputFormat(This,audioInputFormat) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkConfiguration_v7_6_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkVideoOutputCallback_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkVideoOutputCallback_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkVideoOutputCallback_v7_6 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoOutputCallback_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E763A626-4A3C-49D1-BF13-E7AD3692AE52")
    IDeckLinkVideoOutputCallback_v7_6 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE ScheduledFrameCompleted( 
            /* [in] */ IDeckLinkVideoFrame_v7_6 *completedFrame,
            /* [in] */ BMDOutputFrameCompletionResult result) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ScheduledPlaybackHasStopped( void) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoOutputCallback_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoOutputCallback_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoOutputCallback_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoOutputCallback_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *ScheduledFrameCompleted )( 
            IDeckLinkVideoOutputCallback_v7_6 * This,
            /* [in] */ IDeckLinkVideoFrame_v7_6 *completedFrame,
            /* [in] */ BMDOutputFrameCompletionResult result);
        
        HRESULT ( STDMETHODCALLTYPE *ScheduledPlaybackHasStopped )( 
            IDeckLinkVideoOutputCallback_v7_6 * This);
        
        END_INTERFACE
    } IDeckLinkVideoOutputCallback_v7_6Vtbl;

    interface IDeckLinkVideoOutputCallback_v7_6
    {
        CONST_VTBL struct IDeckLinkVideoOutputCallback_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoOutputCallback_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoOutputCallback_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoOutputCallback_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoOutputCallback_v7_6_ScheduledFrameCompleted(This,completedFrame,result)	\
    ( (This)->lpVtbl -> ScheduledFrameCompleted(This,completedFrame,result) ) 

#define IDeckLinkVideoOutputCallback_v7_6_ScheduledPlaybackHasStopped(This)	\
    ( (This)->lpVtbl -> ScheduledPlaybackHasStopped(This) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoOutputCallback_v7_6_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkInputCallback_v7_6_INTERFACE_DEFINED__
#define __IDeckLinkInputCallback_v7_6_INTERFACE_DEFINED__

/* interface IDeckLinkInputCallback_v7_6 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkInputCallback_v7_6;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("31D28EE7-88B6-4CB1-897A-CDBF79A26414")
    IDeckLinkInputCallback_v7_6 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged( 
            /* [in] */ BMDVideoInputFormatChangedEvents notificationEvents,
            /* [in] */ IDeckLinkDisplayMode_v7_6 *newDisplayMode,
            /* [in] */ BMDDetectedVideoInputFormatFlags detectedSignalFlags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived( 
            /* [in] */ IDeckLinkVideoInputFrame_v7_6 *videoFrame,
            /* [in] */ IDeckLinkAudioInputPacket *audioPacket) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkInputCallback_v7_6Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkInputCallback_v7_6 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkInputCallback_v7_6 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkInputCallback_v7_6 * This);
        
        HRESULT ( STDMETHODCALLTYPE *VideoInputFormatChanged )( 
            IDeckLinkInputCallback_v7_6 * This,
            /* [in] */ BMDVideoInputFormatChangedEvents notificationEvents,
            /* [in] */ IDeckLinkDisplayMode_v7_6 *newDisplayMode,
            /* [in] */ BMDDetectedVideoInputFormatFlags detectedSignalFlags);
        
        HRESULT ( STDMETHODCALLTYPE *VideoInputFrameArrived )( 
            IDeckLinkInputCallback_v7_6 * This,
            /* [in] */ IDeckLinkVideoInputFrame_v7_6 *videoFrame,
            /* [in] */ IDeckLinkAudioInputPacket *audioPacket);
        
        END_INTERFACE
    } IDeckLinkInputCallback_v7_6Vtbl;

    interface IDeckLinkInputCallback_v7_6
    {
        CONST_VTBL struct IDeckLinkInputCallback_v7_6Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkInputCallback_v7_6_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkInputCallback_v7_6_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkInputCallback_v7_6_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkInputCallback_v7_6_VideoInputFormatChanged(This,notificationEvents,newDisplayMode,detectedSignalFlags)	\
    ( (This)->lpVtbl -> VideoInputFormatChanged(This,notificationEvents,newDisplayMode,detectedSignalFlags) ) 

#define IDeckLinkInputCallback_v7_6_VideoInputFrameArrived(This,videoFrame,audioPacket)	\
    ( (This)->lpVtbl -> VideoInputFrameArrived(This,videoFrame,audioPacket) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkInputCallback_v7_6_INTERFACE_DEFINED__ */


EXTERN_C const CLSID CLSID_CDeckLinkGLScreenPreviewHelper_v7_6;

#ifdef __cplusplus

class DECLSPEC_UUID("D398CEE7-4434-4CA3-9BA6-5AE34556B905")
CDeckLinkGLScreenPreviewHelper_v7_6;
#endif

EXTERN_C const CLSID CLSID_CDeckLinkVideoConversion_v7_6;

#ifdef __cplusplus

class DECLSPEC_UUID("FFA84F77-73BE-4FB7-B03E-B5E44B9F759B")
CDeckLinkVideoConversion_v7_6;
#endif

#ifndef __IDeckLinkInputCallback_v7_3_INTERFACE_DEFINED__
#define __IDeckLinkInputCallback_v7_3_INTERFACE_DEFINED__

/* interface IDeckLinkInputCallback_v7_3 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkInputCallback_v7_3;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("FD6F311D-4D00-444B-9ED4-1F25B5730AD0")
    IDeckLinkInputCallback_v7_3 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged( 
            /* [in] */ BMDVideoInputFormatChangedEvents notificationEvents,
            /* [in] */ IDeckLinkDisplayMode_v7_6 *newDisplayMode,
            /* [in] */ BMDDetectedVideoInputFormatFlags detectedSignalFlags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived( 
            /* [in] */ IDeckLinkVideoInputFrame_v7_3 *videoFrame,
            /* [in] */ IDeckLinkAudioInputPacket *audioPacket) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkInputCallback_v7_3Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkInputCallback_v7_3 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkInputCallback_v7_3 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkInputCallback_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *VideoInputFormatChanged )( 
            IDeckLinkInputCallback_v7_3 * This,
            /* [in] */ BMDVideoInputFormatChangedEvents notificationEvents,
            /* [in] */ IDeckLinkDisplayMode_v7_6 *newDisplayMode,
            /* [in] */ BMDDetectedVideoInputFormatFlags detectedSignalFlags);
        
        HRESULT ( STDMETHODCALLTYPE *VideoInputFrameArrived )( 
            IDeckLinkInputCallback_v7_3 * This,
            /* [in] */ IDeckLinkVideoInputFrame_v7_3 *videoFrame,
            /* [in] */ IDeckLinkAudioInputPacket *audioPacket);
        
        END_INTERFACE
    } IDeckLinkInputCallback_v7_3Vtbl;

    interface IDeckLinkInputCallback_v7_3
    {
        CONST_VTBL struct IDeckLinkInputCallback_v7_3Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkInputCallback_v7_3_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkInputCallback_v7_3_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkInputCallback_v7_3_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkInputCallback_v7_3_VideoInputFormatChanged(This,notificationEvents,newDisplayMode,detectedSignalFlags)	\
    ( (This)->lpVtbl -> VideoInputFormatChanged(This,notificationEvents,newDisplayMode,detectedSignalFlags) ) 

#define IDeckLinkInputCallback_v7_3_VideoInputFrameArrived(This,videoFrame,audioPacket)	\
    ( (This)->lpVtbl -> VideoInputFrameArrived(This,videoFrame,audioPacket) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkInputCallback_v7_3_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkOutput_v7_3_INTERFACE_DEFINED__
#define __IDeckLinkOutput_v7_3_INTERFACE_DEFINED__

/* interface IDeckLinkOutput_v7_3 */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkOutput_v7_3;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("271C65E3-C323-4344-A30F-D908BCB20AA3")
    IDeckLinkOutput_v7_3 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE DoesSupportVideoMode( 
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            /* [out] */ BMDDisplayModeSupport *result) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetDisplayModeIterator( 
            /* [out] */ IDeckLinkDisplayModeIterator_v7_6 **iterator) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetScreenPreviewCallback( 
            /* [in] */ IDeckLinkScreenPreviewCallback *previewCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableVideoOutput( 
            BMDDisplayMode displayMode,
            BMDVideoOutputFlags flags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableVideoOutput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetVideoOutputFrameMemoryAllocator( 
            /* [in] */ IDeckLinkMemoryAllocator *theAllocator) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE CreateVideoFrame( 
            long width,
            long height,
            long rowBytes,
            BMDPixelFormat pixelFormat,
            BMDFrameFlags flags,
            /* [out] */ IDeckLinkMutableVideoFrame_v7_6 **outFrame) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE CreateAncillaryData( 
            BMDPixelFormat pixelFormat,
            /* [out] */ IDeckLinkVideoFrameAncillary **outBuffer) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisplayVideoFrameSync( 
            /* [in] */ IDeckLinkVideoFrame_v7_6 *theFrame) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ScheduleVideoFrame( 
            /* [in] */ IDeckLinkVideoFrame_v7_6 *theFrame,
            BMDTimeValue displayTime,
            BMDTimeValue displayDuration,
            BMDTimeScale timeScale) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetScheduledFrameCompletionCallback( 
            /* [in] */ IDeckLinkVideoOutputCallback *theCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBufferedVideoFrameCount( 
            /* [out] */ unsigned long *bufferedFrameCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableAudioOutput( 
            BMDAudioSampleRate sampleRate,
            BMDAudioSampleType sampleType,
            unsigned long channelCount,
            BMDAudioOutputStreamType streamType) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableAudioOutput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE WriteAudioSamplesSync( 
            /* [in] */ void *buffer,
            unsigned long sampleFrameCount,
            /* [out] */ unsigned long *sampleFramesWritten) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE BeginAudioPreroll( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EndAudioPreroll( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ScheduleAudioSamples( 
            /* [in] */ void *buffer,
            unsigned long sampleFrameCount,
            BMDTimeValue streamTime,
            BMDTimeScale timeScale,
            /* [out] */ unsigned long *sampleFramesWritten) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBufferedAudioSampleFrameCount( 
            /* [out] */ unsigned long *bufferedSampleFrameCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE FlushBufferedAudioSamples( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetAudioCallback( 
            /* [in] */ IDeckLinkAudioOutputCallback *theCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StartScheduledPlayback( 
            BMDTimeValue playbackStartTime,
            BMDTimeScale timeScale,
            double playbackSpeed) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StopScheduledPlayback( 
            BMDTimeValue stopPlaybackAtTime,
            /* [out] */ BMDTimeValue *actualStopTime,
            BMDTimeScale timeScale) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE IsScheduledPlaybackRunning( 
            /* [out] */ BOOL *active) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetHardwareReferenceClock( 
            BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *elapsedTimeSinceSchedulerBegan) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkOutput_v7_3Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkOutput_v7_3 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkOutput_v7_3 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkOutput_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *DoesSupportVideoMode )( 
            IDeckLinkOutput_v7_3 * This,
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            /* [out] */ BMDDisplayModeSupport *result);
        
        HRESULT ( STDMETHODCALLTYPE *GetDisplayModeIterator )( 
            IDeckLinkOutput_v7_3 * This,
            /* [out] */ IDeckLinkDisplayModeIterator_v7_6 **iterator);
        
        HRESULT ( STDMETHODCALLTYPE *SetScreenPreviewCallback )( 
            IDeckLinkOutput_v7_3 * This,
            /* [in] */ IDeckLinkScreenPreviewCallback *previewCallback);
        
        HRESULT ( STDMETHODCALLTYPE *EnableVideoOutput )( 
            IDeckLinkOutput_v7_3 * This,
            BMDDisplayMode displayMode,
            BMDVideoOutputFlags flags);
        
        HRESULT ( STDMETHODCALLTYPE *DisableVideoOutput )( 
            IDeckLinkOutput_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetVideoOutputFrameMemoryAllocator )( 
            IDeckLinkOutput_v7_3 * This,
            /* [in] */ IDeckLinkMemoryAllocator *theAllocator);
        
        HRESULT ( STDMETHODCALLTYPE *CreateVideoFrame )( 
            IDeckLinkOutput_v7_3 * This,
            long width,
            long height,
            long rowBytes,
            BMDPixelFormat pixelFormat,
            BMDFrameFlags flags,
            /* [out] */ IDeckLinkMutableVideoFrame_v7_6 **outFrame);
        
        HRESULT ( STDMETHODCALLTYPE *CreateAncillaryData )( 
            IDeckLinkOutput_v7_3 * This,
            BMDPixelFormat pixelFormat,
            /* [out] */ IDeckLinkVideoFrameAncillary **outBuffer);
        
        HRESULT ( STDMETHODCALLTYPE *DisplayVideoFrameSync )( 
            IDeckLinkOutput_v7_3 * This,
            /* [in] */ IDeckLinkVideoFrame_v7_6 *theFrame);
        
        HRESULT ( STDMETHODCALLTYPE *ScheduleVideoFrame )( 
            IDeckLinkOutput_v7_3 * This,
            /* [in] */ IDeckLinkVideoFrame_v7_6 *theFrame,
            BMDTimeValue displayTime,
            BMDTimeValue displayDuration,
            BMDTimeScale timeScale);
        
        HRESULT ( STDMETHODCALLTYPE *SetScheduledFrameCompletionCallback )( 
            IDeckLinkOutput_v7_3 * This,
            /* [in] */ IDeckLinkVideoOutputCallback *theCallback);
        
        HRESULT ( STDMETHODCALLTYPE *GetBufferedVideoFrameCount )( 
            IDeckLinkOutput_v7_3 * This,
            /* [out] */ unsigned long *bufferedFrameCount);
        
        HRESULT ( STDMETHODCALLTYPE *EnableAudioOutput )( 
            IDeckLinkOutput_v7_3 * This,
            BMDAudioSampleRate sampleRate,
            BMDAudioSampleType sampleType,
            unsigned long channelCount,
            BMDAudioOutputStreamType streamType);
        
        HRESULT ( STDMETHODCALLTYPE *DisableAudioOutput )( 
            IDeckLinkOutput_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *WriteAudioSamplesSync )( 
            IDeckLinkOutput_v7_3 * This,
            /* [in] */ void *buffer,
            unsigned long sampleFrameCount,
            /* [out] */ unsigned long *sampleFramesWritten);
        
        HRESULT ( STDMETHODCALLTYPE *BeginAudioPreroll )( 
            IDeckLinkOutput_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *EndAudioPreroll )( 
            IDeckLinkOutput_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *ScheduleAudioSamples )( 
            IDeckLinkOutput_v7_3 * This,
            /* [in] */ void *buffer,
            unsigned long sampleFrameCount,
            BMDTimeValue streamTime,
            BMDTimeScale timeScale,
            /* [out] */ unsigned long *sampleFramesWritten);
        
        HRESULT ( STDMETHODCALLTYPE *GetBufferedAudioSampleFrameCount )( 
            IDeckLinkOutput_v7_3 * This,
            /* [out] */ unsigned long *bufferedSampleFrameCount);
        
        HRESULT ( STDMETHODCALLTYPE *FlushBufferedAudioSamples )( 
            IDeckLinkOutput_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetAudioCallback )( 
            IDeckLinkOutput_v7_3 * This,
            /* [in] */ IDeckLinkAudioOutputCallback *theCallback);
        
        HRESULT ( STDMETHODCALLTYPE *StartScheduledPlayback )( 
            IDeckLinkOutput_v7_3 * This,
            BMDTimeValue playbackStartTime,
            BMDTimeScale timeScale,
            double playbackSpeed);
        
        HRESULT ( STDMETHODCALLTYPE *StopScheduledPlayback )( 
            IDeckLinkOutput_v7_3 * This,
            BMDTimeValue stopPlaybackAtTime,
            /* [out] */ BMDTimeValue *actualStopTime,
            BMDTimeScale timeScale);
        
        HRESULT ( STDMETHODCALLTYPE *IsScheduledPlaybackRunning )( 
            IDeckLinkOutput_v7_3 * This,
            /* [out] */ BOOL *active);
        
        HRESULT ( STDMETHODCALLTYPE *GetHardwareReferenceClock )( 
            IDeckLinkOutput_v7_3 * This,
            BMDTimeScale desiredTimeScale,
            /* [out] */ BMDTimeValue *elapsedTimeSinceSchedulerBegan);
        
        END_INTERFACE
    } IDeckLinkOutput_v7_3Vtbl;

    interface IDeckLinkOutput_v7_3
    {
        CONST_VTBL struct IDeckLinkOutput_v7_3Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkOutput_v7_3_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkOutput_v7_3_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkOutput_v7_3_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkOutput_v7_3_DoesSupportVideoMode(This,displayMode,pixelFormat,result)	\
    ( (This)->lpVtbl -> DoesSupportVideoMode(This,displayMode,pixelFormat,result) ) 

#define IDeckLinkOutput_v7_3_GetDisplayModeIterator(This,iterator)	\
    ( (This)->lpVtbl -> GetDisplayModeIterator(This,iterator) ) 

#define IDeckLinkOutput_v7_3_SetScreenPreviewCallback(This,previewCallback)	\
    ( (This)->lpVtbl -> SetScreenPreviewCallback(This,previewCallback) ) 

#define IDeckLinkOutput_v7_3_EnableVideoOutput(This,displayMode,flags)	\
    ( (This)->lpVtbl -> EnableVideoOutput(This,displayMode,flags) ) 

#define IDeckLinkOutput_v7_3_DisableVideoOutput(This)	\
    ( (This)->lpVtbl -> DisableVideoOutput(This) ) 

#define IDeckLinkOutput_v7_3_SetVideoOutputFrameMemoryAllocator(This,theAllocator)	\
    ( (This)->lpVtbl -> SetVideoOutputFrameMemoryAllocator(This,theAllocator) ) 

#define IDeckLinkOutput_v7_3_CreateVideoFrame(This,width,height,rowBytes,pixelFormat,flags,outFrame)	\
    ( (This)->lpVtbl -> CreateVideoFrame(This,width,height,rowBytes,pixelFormat,flags,outFrame) ) 

#define IDeckLinkOutput_v7_3_CreateAncillaryData(This,pixelFormat,outBuffer)	\
    ( (This)->lpVtbl -> CreateAncillaryData(This,pixelFormat,outBuffer) ) 

#define IDeckLinkOutput_v7_3_DisplayVideoFrameSync(This,theFrame)	\
    ( (This)->lpVtbl -> DisplayVideoFrameSync(This,theFrame) ) 

#define IDeckLinkOutput_v7_3_ScheduleVideoFrame(This,theFrame,displayTime,displayDuration,timeScale)	\
    ( (This)->lpVtbl -> ScheduleVideoFrame(This,theFrame,displayTime,displayDuration,timeScale) ) 

#define IDeckLinkOutput_v7_3_SetScheduledFrameCompletionCallback(This,theCallback)	\
    ( (This)->lpVtbl -> SetScheduledFrameCompletionCallback(This,theCallback) ) 

#define IDeckLinkOutput_v7_3_GetBufferedVideoFrameCount(This,bufferedFrameCount)	\
    ( (This)->lpVtbl -> GetBufferedVideoFrameCount(This,bufferedFrameCount) ) 

#define IDeckLinkOutput_v7_3_EnableAudioOutput(This,sampleRate,sampleType,channelCount,streamType)	\
    ( (This)->lpVtbl -> EnableAudioOutput(This,sampleRate,sampleType,channelCount,streamType) ) 

#define IDeckLinkOutput_v7_3_DisableAudioOutput(This)	\
    ( (This)->lpVtbl -> DisableAudioOutput(This) ) 

#define IDeckLinkOutput_v7_3_WriteAudioSamplesSync(This,buffer,sampleFrameCount,sampleFramesWritten)	\
    ( (This)->lpVtbl -> WriteAudioSamplesSync(This,buffer,sampleFrameCount,sampleFramesWritten) ) 

#define IDeckLinkOutput_v7_3_BeginAudioPreroll(This)	\
    ( (This)->lpVtbl -> BeginAudioPreroll(This) ) 

#define IDeckLinkOutput_v7_3_EndAudioPreroll(This)	\
    ( (This)->lpVtbl -> EndAudioPreroll(This) ) 

#define IDeckLinkOutput_v7_3_ScheduleAudioSamples(This,buffer,sampleFrameCount,streamTime,timeScale,sampleFramesWritten)	\
    ( (This)->lpVtbl -> ScheduleAudioSamples(This,buffer,sampleFrameCount,streamTime,timeScale,sampleFramesWritten) ) 

#define IDeckLinkOutput_v7_3_GetBufferedAudioSampleFrameCount(This,bufferedSampleFrameCount)	\
    ( (This)->lpVtbl -> GetBufferedAudioSampleFrameCount(This,bufferedSampleFrameCount) ) 

#define IDeckLinkOutput_v7_3_FlushBufferedAudioSamples(This)	\
    ( (This)->lpVtbl -> FlushBufferedAudioSamples(This) ) 

#define IDeckLinkOutput_v7_3_SetAudioCallback(This,theCallback)	\
    ( (This)->lpVtbl -> SetAudioCallback(This,theCallback) ) 

#define IDeckLinkOutput_v7_3_StartScheduledPlayback(This,playbackStartTime,timeScale,playbackSpeed)	\
    ( (This)->lpVtbl -> StartScheduledPlayback(This,playbackStartTime,timeScale,playbackSpeed) ) 

#define IDeckLinkOutput_v7_3_StopScheduledPlayback(This,stopPlaybackAtTime,actualStopTime,timeScale)	\
    ( (This)->lpVtbl -> StopScheduledPlayback(This,stopPlaybackAtTime,actualStopTime,timeScale) ) 

#define IDeckLinkOutput_v7_3_IsScheduledPlaybackRunning(This,active)	\
    ( (This)->lpVtbl -> IsScheduledPlaybackRunning(This,active) ) 

#define IDeckLinkOutput_v7_3_GetHardwareReferenceClock(This,desiredTimeScale,elapsedTimeSinceSchedulerBegan)	\
    ( (This)->lpVtbl -> GetHardwareReferenceClock(This,desiredTimeScale,elapsedTimeSinceSchedulerBegan) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkOutput_v7_3_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkInput_v7_3_INTERFACE_DEFINED__
#define __IDeckLinkInput_v7_3_INTERFACE_DEFINED__

/* interface IDeckLinkInput_v7_3 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkInput_v7_3;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("4973F012-9925-458C-871C-18774CDBBECB")
    IDeckLinkInput_v7_3 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE DoesSupportVideoMode( 
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            /* [out] */ BMDDisplayModeSupport *result) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetDisplayModeIterator( 
            /* [out] */ IDeckLinkDisplayModeIterator_v7_6 **iterator) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetScreenPreviewCallback( 
            /* [in] */ IDeckLinkScreenPreviewCallback *previewCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableVideoInput( 
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            BMDVideoInputFlags flags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableVideoInput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetAvailableVideoFrameCount( 
            /* [out] */ unsigned long *availableFrameCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableAudioInput( 
            BMDAudioSampleRate sampleRate,
            BMDAudioSampleType sampleType,
            unsigned long channelCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableAudioInput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetAvailableAudioSampleFrameCount( 
            /* [out] */ unsigned long *availableSampleFrameCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StartStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StopStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE PauseStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE FlushStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetCallback( 
            /* [in] */ IDeckLinkInputCallback_v7_3 *theCallback) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkInput_v7_3Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkInput_v7_3 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkInput_v7_3 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkInput_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *DoesSupportVideoMode )( 
            IDeckLinkInput_v7_3 * This,
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            /* [out] */ BMDDisplayModeSupport *result);
        
        HRESULT ( STDMETHODCALLTYPE *GetDisplayModeIterator )( 
            IDeckLinkInput_v7_3 * This,
            /* [out] */ IDeckLinkDisplayModeIterator_v7_6 **iterator);
        
        HRESULT ( STDMETHODCALLTYPE *SetScreenPreviewCallback )( 
            IDeckLinkInput_v7_3 * This,
            /* [in] */ IDeckLinkScreenPreviewCallback *previewCallback);
        
        HRESULT ( STDMETHODCALLTYPE *EnableVideoInput )( 
            IDeckLinkInput_v7_3 * This,
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            BMDVideoInputFlags flags);
        
        HRESULT ( STDMETHODCALLTYPE *DisableVideoInput )( 
            IDeckLinkInput_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetAvailableVideoFrameCount )( 
            IDeckLinkInput_v7_3 * This,
            /* [out] */ unsigned long *availableFrameCount);
        
        HRESULT ( STDMETHODCALLTYPE *EnableAudioInput )( 
            IDeckLinkInput_v7_3 * This,
            BMDAudioSampleRate sampleRate,
            BMDAudioSampleType sampleType,
            unsigned long channelCount);
        
        HRESULT ( STDMETHODCALLTYPE *DisableAudioInput )( 
            IDeckLinkInput_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetAvailableAudioSampleFrameCount )( 
            IDeckLinkInput_v7_3 * This,
            /* [out] */ unsigned long *availableSampleFrameCount);
        
        HRESULT ( STDMETHODCALLTYPE *StartStreams )( 
            IDeckLinkInput_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *StopStreams )( 
            IDeckLinkInput_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *PauseStreams )( 
            IDeckLinkInput_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *FlushStreams )( 
            IDeckLinkInput_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetCallback )( 
            IDeckLinkInput_v7_3 * This,
            /* [in] */ IDeckLinkInputCallback_v7_3 *theCallback);
        
        END_INTERFACE
    } IDeckLinkInput_v7_3Vtbl;

    interface IDeckLinkInput_v7_3
    {
        CONST_VTBL struct IDeckLinkInput_v7_3Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkInput_v7_3_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkInput_v7_3_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkInput_v7_3_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkInput_v7_3_DoesSupportVideoMode(This,displayMode,pixelFormat,result)	\
    ( (This)->lpVtbl -> DoesSupportVideoMode(This,displayMode,pixelFormat,result) ) 

#define IDeckLinkInput_v7_3_GetDisplayModeIterator(This,iterator)	\
    ( (This)->lpVtbl -> GetDisplayModeIterator(This,iterator) ) 

#define IDeckLinkInput_v7_3_SetScreenPreviewCallback(This,previewCallback)	\
    ( (This)->lpVtbl -> SetScreenPreviewCallback(This,previewCallback) ) 

#define IDeckLinkInput_v7_3_EnableVideoInput(This,displayMode,pixelFormat,flags)	\
    ( (This)->lpVtbl -> EnableVideoInput(This,displayMode,pixelFormat,flags) ) 

#define IDeckLinkInput_v7_3_DisableVideoInput(This)	\
    ( (This)->lpVtbl -> DisableVideoInput(This) ) 

#define IDeckLinkInput_v7_3_GetAvailableVideoFrameCount(This,availableFrameCount)	\
    ( (This)->lpVtbl -> GetAvailableVideoFrameCount(This,availableFrameCount) ) 

#define IDeckLinkInput_v7_3_EnableAudioInput(This,sampleRate,sampleType,channelCount)	\
    ( (This)->lpVtbl -> EnableAudioInput(This,sampleRate,sampleType,channelCount) ) 

#define IDeckLinkInput_v7_3_DisableAudioInput(This)	\
    ( (This)->lpVtbl -> DisableAudioInput(This) ) 

#define IDeckLinkInput_v7_3_GetAvailableAudioSampleFrameCount(This,availableSampleFrameCount)	\
    ( (This)->lpVtbl -> GetAvailableAudioSampleFrameCount(This,availableSampleFrameCount) ) 

#define IDeckLinkInput_v7_3_StartStreams(This)	\
    ( (This)->lpVtbl -> StartStreams(This) ) 

#define IDeckLinkInput_v7_3_StopStreams(This)	\
    ( (This)->lpVtbl -> StopStreams(This) ) 

#define IDeckLinkInput_v7_3_PauseStreams(This)	\
    ( (This)->lpVtbl -> PauseStreams(This) ) 

#define IDeckLinkInput_v7_3_FlushStreams(This)	\
    ( (This)->lpVtbl -> FlushStreams(This) ) 

#define IDeckLinkInput_v7_3_SetCallback(This,theCallback)	\
    ( (This)->lpVtbl -> SetCallback(This,theCallback) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkInput_v7_3_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkVideoInputFrame_v7_3_INTERFACE_DEFINED__
#define __IDeckLinkVideoInputFrame_v7_3_INTERFACE_DEFINED__

/* interface IDeckLinkVideoInputFrame_v7_3 */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoInputFrame_v7_3;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("CF317790-2894-11DE-8C30-0800200C9A66")
    IDeckLinkVideoInputFrame_v7_3 : public IDeckLinkVideoFrame_v7_6
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetStreamTime( 
            /* [out] */ BMDTimeValue *frameTime,
            /* [out] */ BMDTimeValue *frameDuration,
            BMDTimeScale timeScale) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoInputFrame_v7_3Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoInputFrame_v7_3 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoInputFrame_v7_3 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoInputFrame_v7_3 * This);
        
        long ( STDMETHODCALLTYPE *GetWidth )( 
            IDeckLinkVideoInputFrame_v7_3 * This);
        
        long ( STDMETHODCALLTYPE *GetHeight )( 
            IDeckLinkVideoInputFrame_v7_3 * This);
        
        long ( STDMETHODCALLTYPE *GetRowBytes )( 
            IDeckLinkVideoInputFrame_v7_3 * This);
        
        BMDPixelFormat ( STDMETHODCALLTYPE *GetPixelFormat )( 
            IDeckLinkVideoInputFrame_v7_3 * This);
        
        BMDFrameFlags ( STDMETHODCALLTYPE *GetFlags )( 
            IDeckLinkVideoInputFrame_v7_3 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetBytes )( 
            IDeckLinkVideoInputFrame_v7_3 * This,
            /* [out] */ void **buffer);
        
        HRESULT ( STDMETHODCALLTYPE *GetTimecode )( 
            IDeckLinkVideoInputFrame_v7_3 * This,
            BMDTimecodeFormat format,
            /* [out] */ IDeckLinkTimecode_v7_6 **timecode);
        
        HRESULT ( STDMETHODCALLTYPE *GetAncillaryData )( 
            IDeckLinkVideoInputFrame_v7_3 * This,
            /* [out] */ IDeckLinkVideoFrameAncillary **ancillary);
        
        HRESULT ( STDMETHODCALLTYPE *GetStreamTime )( 
            IDeckLinkVideoInputFrame_v7_3 * This,
            /* [out] */ BMDTimeValue *frameTime,
            /* [out] */ BMDTimeValue *frameDuration,
            BMDTimeScale timeScale);
        
        END_INTERFACE
    } IDeckLinkVideoInputFrame_v7_3Vtbl;

    interface IDeckLinkVideoInputFrame_v7_3
    {
        CONST_VTBL struct IDeckLinkVideoInputFrame_v7_3Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoInputFrame_v7_3_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoInputFrame_v7_3_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoInputFrame_v7_3_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoInputFrame_v7_3_GetWidth(This)	\
    ( (This)->lpVtbl -> GetWidth(This) ) 

#define IDeckLinkVideoInputFrame_v7_3_GetHeight(This)	\
    ( (This)->lpVtbl -> GetHeight(This) ) 

#define IDeckLinkVideoInputFrame_v7_3_GetRowBytes(This)	\
    ( (This)->lpVtbl -> GetRowBytes(This) ) 

#define IDeckLinkVideoInputFrame_v7_3_GetPixelFormat(This)	\
    ( (This)->lpVtbl -> GetPixelFormat(This) ) 

#define IDeckLinkVideoInputFrame_v7_3_GetFlags(This)	\
    ( (This)->lpVtbl -> GetFlags(This) ) 

#define IDeckLinkVideoInputFrame_v7_3_GetBytes(This,buffer)	\
    ( (This)->lpVtbl -> GetBytes(This,buffer) ) 

#define IDeckLinkVideoInputFrame_v7_3_GetTimecode(This,format,timecode)	\
    ( (This)->lpVtbl -> GetTimecode(This,format,timecode) ) 

#define IDeckLinkVideoInputFrame_v7_3_GetAncillaryData(This,ancillary)	\
    ( (This)->lpVtbl -> GetAncillaryData(This,ancillary) ) 


#define IDeckLinkVideoInputFrame_v7_3_GetStreamTime(This,frameTime,frameDuration,timeScale)	\
    ( (This)->lpVtbl -> GetStreamTime(This,frameTime,frameDuration,timeScale) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoInputFrame_v7_3_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkDisplayModeIterator_v7_1_INTERFACE_DEFINED__
#define __IDeckLinkDisplayModeIterator_v7_1_INTERFACE_DEFINED__

/* interface IDeckLinkDisplayModeIterator_v7_1 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkDisplayModeIterator_v7_1;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("B28131B6-59AC-4857-B5AC-CD75D5883E2F")
    IDeckLinkDisplayModeIterator_v7_1 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE Next( 
            /* [out] */ IDeckLinkDisplayMode_v7_1 **deckLinkDisplayMode) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkDisplayModeIterator_v7_1Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkDisplayModeIterator_v7_1 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkDisplayModeIterator_v7_1 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkDisplayModeIterator_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *Next )( 
            IDeckLinkDisplayModeIterator_v7_1 * This,
            /* [out] */ IDeckLinkDisplayMode_v7_1 **deckLinkDisplayMode);
        
        END_INTERFACE
    } IDeckLinkDisplayModeIterator_v7_1Vtbl;

    interface IDeckLinkDisplayModeIterator_v7_1
    {
        CONST_VTBL struct IDeckLinkDisplayModeIterator_v7_1Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkDisplayModeIterator_v7_1_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkDisplayModeIterator_v7_1_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkDisplayModeIterator_v7_1_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkDisplayModeIterator_v7_1_Next(This,deckLinkDisplayMode)	\
    ( (This)->lpVtbl -> Next(This,deckLinkDisplayMode) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkDisplayModeIterator_v7_1_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkDisplayMode_v7_1_INTERFACE_DEFINED__
#define __IDeckLinkDisplayMode_v7_1_INTERFACE_DEFINED__

/* interface IDeckLinkDisplayMode_v7_1 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkDisplayMode_v7_1;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("AF0CD6D5-8376-435E-8433-54F9DD530AC3")
    IDeckLinkDisplayMode_v7_1 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetName( 
            /* [out] */ BSTR *name) = 0;
        
        virtual BMDDisplayMode STDMETHODCALLTYPE GetDisplayMode( void) = 0;
        
        virtual long STDMETHODCALLTYPE GetWidth( void) = 0;
        
        virtual long STDMETHODCALLTYPE GetHeight( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetFrameRate( 
            /* [out] */ BMDTimeValue *frameDuration,
            /* [out] */ BMDTimeScale *timeScale) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkDisplayMode_v7_1Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkDisplayMode_v7_1 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkDisplayMode_v7_1 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkDisplayMode_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetName )( 
            IDeckLinkDisplayMode_v7_1 * This,
            /* [out] */ BSTR *name);
        
        BMDDisplayMode ( STDMETHODCALLTYPE *GetDisplayMode )( 
            IDeckLinkDisplayMode_v7_1 * This);
        
        long ( STDMETHODCALLTYPE *GetWidth )( 
            IDeckLinkDisplayMode_v7_1 * This);
        
        long ( STDMETHODCALLTYPE *GetHeight )( 
            IDeckLinkDisplayMode_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetFrameRate )( 
            IDeckLinkDisplayMode_v7_1 * This,
            /* [out] */ BMDTimeValue *frameDuration,
            /* [out] */ BMDTimeScale *timeScale);
        
        END_INTERFACE
    } IDeckLinkDisplayMode_v7_1Vtbl;

    interface IDeckLinkDisplayMode_v7_1
    {
        CONST_VTBL struct IDeckLinkDisplayMode_v7_1Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkDisplayMode_v7_1_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkDisplayMode_v7_1_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkDisplayMode_v7_1_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkDisplayMode_v7_1_GetName(This,name)	\
    ( (This)->lpVtbl -> GetName(This,name) ) 

#define IDeckLinkDisplayMode_v7_1_GetDisplayMode(This)	\
    ( (This)->lpVtbl -> GetDisplayMode(This) ) 

#define IDeckLinkDisplayMode_v7_1_GetWidth(This)	\
    ( (This)->lpVtbl -> GetWidth(This) ) 

#define IDeckLinkDisplayMode_v7_1_GetHeight(This)	\
    ( (This)->lpVtbl -> GetHeight(This) ) 

#define IDeckLinkDisplayMode_v7_1_GetFrameRate(This,frameDuration,timeScale)	\
    ( (This)->lpVtbl -> GetFrameRate(This,frameDuration,timeScale) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkDisplayMode_v7_1_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkVideoFrame_v7_1_INTERFACE_DEFINED__
#define __IDeckLinkVideoFrame_v7_1_INTERFACE_DEFINED__

/* interface IDeckLinkVideoFrame_v7_1 */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoFrame_v7_1;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("333F3A10-8C2D-43CF-B79D-46560FEEA1CE")
    IDeckLinkVideoFrame_v7_1 : public IUnknown
    {
    public:
        virtual long STDMETHODCALLTYPE GetWidth( void) = 0;
        
        virtual long STDMETHODCALLTYPE GetHeight( void) = 0;
        
        virtual long STDMETHODCALLTYPE GetRowBytes( void) = 0;
        
        virtual BMDPixelFormat STDMETHODCALLTYPE GetPixelFormat( void) = 0;
        
        virtual BMDFrameFlags STDMETHODCALLTYPE GetFlags( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBytes( 
            void **buffer) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoFrame_v7_1Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoFrame_v7_1 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoFrame_v7_1 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoFrame_v7_1 * This);
        
        long ( STDMETHODCALLTYPE *GetWidth )( 
            IDeckLinkVideoFrame_v7_1 * This);
        
        long ( STDMETHODCALLTYPE *GetHeight )( 
            IDeckLinkVideoFrame_v7_1 * This);
        
        long ( STDMETHODCALLTYPE *GetRowBytes )( 
            IDeckLinkVideoFrame_v7_1 * This);
        
        BMDPixelFormat ( STDMETHODCALLTYPE *GetPixelFormat )( 
            IDeckLinkVideoFrame_v7_1 * This);
        
        BMDFrameFlags ( STDMETHODCALLTYPE *GetFlags )( 
            IDeckLinkVideoFrame_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetBytes )( 
            IDeckLinkVideoFrame_v7_1 * This,
            void **buffer);
        
        END_INTERFACE
    } IDeckLinkVideoFrame_v7_1Vtbl;

    interface IDeckLinkVideoFrame_v7_1
    {
        CONST_VTBL struct IDeckLinkVideoFrame_v7_1Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoFrame_v7_1_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoFrame_v7_1_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoFrame_v7_1_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoFrame_v7_1_GetWidth(This)	\
    ( (This)->lpVtbl -> GetWidth(This) ) 

#define IDeckLinkVideoFrame_v7_1_GetHeight(This)	\
    ( (This)->lpVtbl -> GetHeight(This) ) 

#define IDeckLinkVideoFrame_v7_1_GetRowBytes(This)	\
    ( (This)->lpVtbl -> GetRowBytes(This) ) 

#define IDeckLinkVideoFrame_v7_1_GetPixelFormat(This)	\
    ( (This)->lpVtbl -> GetPixelFormat(This) ) 

#define IDeckLinkVideoFrame_v7_1_GetFlags(This)	\
    ( (This)->lpVtbl -> GetFlags(This) ) 

#define IDeckLinkVideoFrame_v7_1_GetBytes(This,buffer)	\
    ( (This)->lpVtbl -> GetBytes(This,buffer) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoFrame_v7_1_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkVideoInputFrame_v7_1_INTERFACE_DEFINED__
#define __IDeckLinkVideoInputFrame_v7_1_INTERFACE_DEFINED__

/* interface IDeckLinkVideoInputFrame_v7_1 */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoInputFrame_v7_1;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("C8B41D95-8848-40EE-9B37-6E3417FB114B")
    IDeckLinkVideoInputFrame_v7_1 : public IDeckLinkVideoFrame_v7_1
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetFrameTime( 
            BMDTimeValue *frameTime,
            BMDTimeValue *frameDuration,
            BMDTimeScale timeScale) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoInputFrame_v7_1Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoInputFrame_v7_1 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoInputFrame_v7_1 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoInputFrame_v7_1 * This);
        
        long ( STDMETHODCALLTYPE *GetWidth )( 
            IDeckLinkVideoInputFrame_v7_1 * This);
        
        long ( STDMETHODCALLTYPE *GetHeight )( 
            IDeckLinkVideoInputFrame_v7_1 * This);
        
        long ( STDMETHODCALLTYPE *GetRowBytes )( 
            IDeckLinkVideoInputFrame_v7_1 * This);
        
        BMDPixelFormat ( STDMETHODCALLTYPE *GetPixelFormat )( 
            IDeckLinkVideoInputFrame_v7_1 * This);
        
        BMDFrameFlags ( STDMETHODCALLTYPE *GetFlags )( 
            IDeckLinkVideoInputFrame_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetBytes )( 
            IDeckLinkVideoInputFrame_v7_1 * This,
            void **buffer);
        
        HRESULT ( STDMETHODCALLTYPE *GetFrameTime )( 
            IDeckLinkVideoInputFrame_v7_1 * This,
            BMDTimeValue *frameTime,
            BMDTimeValue *frameDuration,
            BMDTimeScale timeScale);
        
        END_INTERFACE
    } IDeckLinkVideoInputFrame_v7_1Vtbl;

    interface IDeckLinkVideoInputFrame_v7_1
    {
        CONST_VTBL struct IDeckLinkVideoInputFrame_v7_1Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoInputFrame_v7_1_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoInputFrame_v7_1_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoInputFrame_v7_1_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoInputFrame_v7_1_GetWidth(This)	\
    ( (This)->lpVtbl -> GetWidth(This) ) 

#define IDeckLinkVideoInputFrame_v7_1_GetHeight(This)	\
    ( (This)->lpVtbl -> GetHeight(This) ) 

#define IDeckLinkVideoInputFrame_v7_1_GetRowBytes(This)	\
    ( (This)->lpVtbl -> GetRowBytes(This) ) 

#define IDeckLinkVideoInputFrame_v7_1_GetPixelFormat(This)	\
    ( (This)->lpVtbl -> GetPixelFormat(This) ) 

#define IDeckLinkVideoInputFrame_v7_1_GetFlags(This)	\
    ( (This)->lpVtbl -> GetFlags(This) ) 

#define IDeckLinkVideoInputFrame_v7_1_GetBytes(This,buffer)	\
    ( (This)->lpVtbl -> GetBytes(This,buffer) ) 


#define IDeckLinkVideoInputFrame_v7_1_GetFrameTime(This,frameTime,frameDuration,timeScale)	\
    ( (This)->lpVtbl -> GetFrameTime(This,frameTime,frameDuration,timeScale) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoInputFrame_v7_1_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkAudioInputPacket_v7_1_INTERFACE_DEFINED__
#define __IDeckLinkAudioInputPacket_v7_1_INTERFACE_DEFINED__

/* interface IDeckLinkAudioInputPacket_v7_1 */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkAudioInputPacket_v7_1;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("C86DE4F6-A29F-42E3-AB3A-1363E29F0788")
    IDeckLinkAudioInputPacket_v7_1 : public IUnknown
    {
    public:
        virtual long STDMETHODCALLTYPE GetSampleCount( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBytes( 
            void **buffer) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetAudioPacketTime( 
            BMDTimeValue *packetTime,
            BMDTimeScale timeScale) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkAudioInputPacket_v7_1Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkAudioInputPacket_v7_1 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkAudioInputPacket_v7_1 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkAudioInputPacket_v7_1 * This);
        
        long ( STDMETHODCALLTYPE *GetSampleCount )( 
            IDeckLinkAudioInputPacket_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *GetBytes )( 
            IDeckLinkAudioInputPacket_v7_1 * This,
            void **buffer);
        
        HRESULT ( STDMETHODCALLTYPE *GetAudioPacketTime )( 
            IDeckLinkAudioInputPacket_v7_1 * This,
            BMDTimeValue *packetTime,
            BMDTimeScale timeScale);
        
        END_INTERFACE
    } IDeckLinkAudioInputPacket_v7_1Vtbl;

    interface IDeckLinkAudioInputPacket_v7_1
    {
        CONST_VTBL struct IDeckLinkAudioInputPacket_v7_1Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkAudioInputPacket_v7_1_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkAudioInputPacket_v7_1_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkAudioInputPacket_v7_1_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkAudioInputPacket_v7_1_GetSampleCount(This)	\
    ( (This)->lpVtbl -> GetSampleCount(This) ) 

#define IDeckLinkAudioInputPacket_v7_1_GetBytes(This,buffer)	\
    ( (This)->lpVtbl -> GetBytes(This,buffer) ) 

#define IDeckLinkAudioInputPacket_v7_1_GetAudioPacketTime(This,packetTime,timeScale)	\
    ( (This)->lpVtbl -> GetAudioPacketTime(This,packetTime,timeScale) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkAudioInputPacket_v7_1_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkVideoOutputCallback_v7_1_INTERFACE_DEFINED__
#define __IDeckLinkVideoOutputCallback_v7_1_INTERFACE_DEFINED__

/* interface IDeckLinkVideoOutputCallback_v7_1 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkVideoOutputCallback_v7_1;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("EBD01AFA-E4B0-49C6-A01D-EDB9D1B55FD9")
    IDeckLinkVideoOutputCallback_v7_1 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE ScheduledFrameCompleted( 
            /* [in] */ IDeckLinkVideoFrame_v7_1 *completedFrame,
            /* [in] */ BMDOutputFrameCompletionResult result) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkVideoOutputCallback_v7_1Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkVideoOutputCallback_v7_1 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkVideoOutputCallback_v7_1 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkVideoOutputCallback_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *ScheduledFrameCompleted )( 
            IDeckLinkVideoOutputCallback_v7_1 * This,
            /* [in] */ IDeckLinkVideoFrame_v7_1 *completedFrame,
            /* [in] */ BMDOutputFrameCompletionResult result);
        
        END_INTERFACE
    } IDeckLinkVideoOutputCallback_v7_1Vtbl;

    interface IDeckLinkVideoOutputCallback_v7_1
    {
        CONST_VTBL struct IDeckLinkVideoOutputCallback_v7_1Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkVideoOutputCallback_v7_1_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkVideoOutputCallback_v7_1_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkVideoOutputCallback_v7_1_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkVideoOutputCallback_v7_1_ScheduledFrameCompleted(This,completedFrame,result)	\
    ( (This)->lpVtbl -> ScheduledFrameCompleted(This,completedFrame,result) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkVideoOutputCallback_v7_1_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkInputCallback_v7_1_INTERFACE_DEFINED__
#define __IDeckLinkInputCallback_v7_1_INTERFACE_DEFINED__

/* interface IDeckLinkInputCallback_v7_1 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkInputCallback_v7_1;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("7F94F328-5ED4-4E9F-9729-76A86BDC99CC")
    IDeckLinkInputCallback_v7_1 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived( 
            /* [in] */ IDeckLinkVideoInputFrame_v7_1 *videoFrame,
            /* [in] */ IDeckLinkAudioInputPacket_v7_1 *audioPacket) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkInputCallback_v7_1Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkInputCallback_v7_1 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkInputCallback_v7_1 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkInputCallback_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *VideoInputFrameArrived )( 
            IDeckLinkInputCallback_v7_1 * This,
            /* [in] */ IDeckLinkVideoInputFrame_v7_1 *videoFrame,
            /* [in] */ IDeckLinkAudioInputPacket_v7_1 *audioPacket);
        
        END_INTERFACE
    } IDeckLinkInputCallback_v7_1Vtbl;

    interface IDeckLinkInputCallback_v7_1
    {
        CONST_VTBL struct IDeckLinkInputCallback_v7_1Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkInputCallback_v7_1_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkInputCallback_v7_1_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkInputCallback_v7_1_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkInputCallback_v7_1_VideoInputFrameArrived(This,videoFrame,audioPacket)	\
    ( (This)->lpVtbl -> VideoInputFrameArrived(This,videoFrame,audioPacket) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkInputCallback_v7_1_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkOutput_v7_1_INTERFACE_DEFINED__
#define __IDeckLinkOutput_v7_1_INTERFACE_DEFINED__

/* interface IDeckLinkOutput_v7_1 */
/* [helpstring][local][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkOutput_v7_1;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("AE5B3E9B-4E1E-4535-B6E8-480FF52F6CE5")
    IDeckLinkOutput_v7_1 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE DoesSupportVideoMode( 
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            /* [out] */ BMDDisplayModeSupport *result) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetDisplayModeIterator( 
            /* [out] */ IDeckLinkDisplayModeIterator_v7_1 **iterator) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableVideoOutput( 
            BMDDisplayMode displayMode) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableVideoOutput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetVideoOutputFrameMemoryAllocator( 
            /* [in] */ IDeckLinkMemoryAllocator *theAllocator) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE CreateVideoFrame( 
            long width,
            long height,
            long rowBytes,
            BMDPixelFormat pixelFormat,
            BMDFrameFlags flags,
            IDeckLinkVideoFrame_v7_1 **outFrame) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE CreateVideoFrameFromBuffer( 
            void *buffer,
            long width,
            long height,
            long rowBytes,
            BMDPixelFormat pixelFormat,
            BMDFrameFlags flags,
            IDeckLinkVideoFrame_v7_1 **outFrame) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisplayVideoFrameSync( 
            IDeckLinkVideoFrame_v7_1 *theFrame) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ScheduleVideoFrame( 
            IDeckLinkVideoFrame_v7_1 *theFrame,
            BMDTimeValue displayTime,
            BMDTimeValue displayDuration,
            BMDTimeScale timeScale) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetScheduledFrameCompletionCallback( 
            /* [in] */ IDeckLinkVideoOutputCallback_v7_1 *theCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableAudioOutput( 
            BMDAudioSampleRate sampleRate,
            BMDAudioSampleType sampleType,
            unsigned long channelCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableAudioOutput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE WriteAudioSamplesSync( 
            void *buffer,
            unsigned long sampleFrameCount,
            /* [out] */ unsigned long *sampleFramesWritten) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE BeginAudioPreroll( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EndAudioPreroll( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ScheduleAudioSamples( 
            void *buffer,
            unsigned long sampleFrameCount,
            BMDTimeValue streamTime,
            BMDTimeScale timeScale,
            /* [out] */ unsigned long *sampleFramesWritten) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBufferedAudioSampleFrameCount( 
            /* [out] */ unsigned long *bufferedSampleCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE FlushBufferedAudioSamples( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetAudioCallback( 
            /* [in] */ IDeckLinkAudioOutputCallback *theCallback) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StartScheduledPlayback( 
            BMDTimeValue playbackStartTime,
            BMDTimeScale timeScale,
            double playbackSpeed) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StopScheduledPlayback( 
            BMDTimeValue stopPlaybackAtTime,
            BMDTimeValue *actualStopTime,
            BMDTimeScale timeScale) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetHardwareReferenceClock( 
            BMDTimeScale desiredTimeScale,
            BMDTimeValue *elapsedTimeSinceSchedulerBegan) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkOutput_v7_1Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkOutput_v7_1 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkOutput_v7_1 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkOutput_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *DoesSupportVideoMode )( 
            IDeckLinkOutput_v7_1 * This,
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            /* [out] */ BMDDisplayModeSupport *result);
        
        HRESULT ( STDMETHODCALLTYPE *GetDisplayModeIterator )( 
            IDeckLinkOutput_v7_1 * This,
            /* [out] */ IDeckLinkDisplayModeIterator_v7_1 **iterator);
        
        HRESULT ( STDMETHODCALLTYPE *EnableVideoOutput )( 
            IDeckLinkOutput_v7_1 * This,
            BMDDisplayMode displayMode);
        
        HRESULT ( STDMETHODCALLTYPE *DisableVideoOutput )( 
            IDeckLinkOutput_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetVideoOutputFrameMemoryAllocator )( 
            IDeckLinkOutput_v7_1 * This,
            /* [in] */ IDeckLinkMemoryAllocator *theAllocator);
        
        HRESULT ( STDMETHODCALLTYPE *CreateVideoFrame )( 
            IDeckLinkOutput_v7_1 * This,
            long width,
            long height,
            long rowBytes,
            BMDPixelFormat pixelFormat,
            BMDFrameFlags flags,
            IDeckLinkVideoFrame_v7_1 **outFrame);
        
        HRESULT ( STDMETHODCALLTYPE *CreateVideoFrameFromBuffer )( 
            IDeckLinkOutput_v7_1 * This,
            void *buffer,
            long width,
            long height,
            long rowBytes,
            BMDPixelFormat pixelFormat,
            BMDFrameFlags flags,
            IDeckLinkVideoFrame_v7_1 **outFrame);
        
        HRESULT ( STDMETHODCALLTYPE *DisplayVideoFrameSync )( 
            IDeckLinkOutput_v7_1 * This,
            IDeckLinkVideoFrame_v7_1 *theFrame);
        
        HRESULT ( STDMETHODCALLTYPE *ScheduleVideoFrame )( 
            IDeckLinkOutput_v7_1 * This,
            IDeckLinkVideoFrame_v7_1 *theFrame,
            BMDTimeValue displayTime,
            BMDTimeValue displayDuration,
            BMDTimeScale timeScale);
        
        HRESULT ( STDMETHODCALLTYPE *SetScheduledFrameCompletionCallback )( 
            IDeckLinkOutput_v7_1 * This,
            /* [in] */ IDeckLinkVideoOutputCallback_v7_1 *theCallback);
        
        HRESULT ( STDMETHODCALLTYPE *EnableAudioOutput )( 
            IDeckLinkOutput_v7_1 * This,
            BMDAudioSampleRate sampleRate,
            BMDAudioSampleType sampleType,
            unsigned long channelCount);
        
        HRESULT ( STDMETHODCALLTYPE *DisableAudioOutput )( 
            IDeckLinkOutput_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *WriteAudioSamplesSync )( 
            IDeckLinkOutput_v7_1 * This,
            void *buffer,
            unsigned long sampleFrameCount,
            /* [out] */ unsigned long *sampleFramesWritten);
        
        HRESULT ( STDMETHODCALLTYPE *BeginAudioPreroll )( 
            IDeckLinkOutput_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *EndAudioPreroll )( 
            IDeckLinkOutput_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *ScheduleAudioSamples )( 
            IDeckLinkOutput_v7_1 * This,
            void *buffer,
            unsigned long sampleFrameCount,
            BMDTimeValue streamTime,
            BMDTimeScale timeScale,
            /* [out] */ unsigned long *sampleFramesWritten);
        
        HRESULT ( STDMETHODCALLTYPE *GetBufferedAudioSampleFrameCount )( 
            IDeckLinkOutput_v7_1 * This,
            /* [out] */ unsigned long *bufferedSampleCount);
        
        HRESULT ( STDMETHODCALLTYPE *FlushBufferedAudioSamples )( 
            IDeckLinkOutput_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetAudioCallback )( 
            IDeckLinkOutput_v7_1 * This,
            /* [in] */ IDeckLinkAudioOutputCallback *theCallback);
        
        HRESULT ( STDMETHODCALLTYPE *StartScheduledPlayback )( 
            IDeckLinkOutput_v7_1 * This,
            BMDTimeValue playbackStartTime,
            BMDTimeScale timeScale,
            double playbackSpeed);
        
        HRESULT ( STDMETHODCALLTYPE *StopScheduledPlayback )( 
            IDeckLinkOutput_v7_1 * This,
            BMDTimeValue stopPlaybackAtTime,
            BMDTimeValue *actualStopTime,
            BMDTimeScale timeScale);
        
        HRESULT ( STDMETHODCALLTYPE *GetHardwareReferenceClock )( 
            IDeckLinkOutput_v7_1 * This,
            BMDTimeScale desiredTimeScale,
            BMDTimeValue *elapsedTimeSinceSchedulerBegan);
        
        END_INTERFACE
    } IDeckLinkOutput_v7_1Vtbl;

    interface IDeckLinkOutput_v7_1
    {
        CONST_VTBL struct IDeckLinkOutput_v7_1Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkOutput_v7_1_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkOutput_v7_1_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkOutput_v7_1_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkOutput_v7_1_DoesSupportVideoMode(This,displayMode,pixelFormat,result)	\
    ( (This)->lpVtbl -> DoesSupportVideoMode(This,displayMode,pixelFormat,result) ) 

#define IDeckLinkOutput_v7_1_GetDisplayModeIterator(This,iterator)	\
    ( (This)->lpVtbl -> GetDisplayModeIterator(This,iterator) ) 

#define IDeckLinkOutput_v7_1_EnableVideoOutput(This,displayMode)	\
    ( (This)->lpVtbl -> EnableVideoOutput(This,displayMode) ) 

#define IDeckLinkOutput_v7_1_DisableVideoOutput(This)	\
    ( (This)->lpVtbl -> DisableVideoOutput(This) ) 

#define IDeckLinkOutput_v7_1_SetVideoOutputFrameMemoryAllocator(This,theAllocator)	\
    ( (This)->lpVtbl -> SetVideoOutputFrameMemoryAllocator(This,theAllocator) ) 

#define IDeckLinkOutput_v7_1_CreateVideoFrame(This,width,height,rowBytes,pixelFormat,flags,outFrame)	\
    ( (This)->lpVtbl -> CreateVideoFrame(This,width,height,rowBytes,pixelFormat,flags,outFrame) ) 

#define IDeckLinkOutput_v7_1_CreateVideoFrameFromBuffer(This,buffer,width,height,rowBytes,pixelFormat,flags,outFrame)	\
    ( (This)->lpVtbl -> CreateVideoFrameFromBuffer(This,buffer,width,height,rowBytes,pixelFormat,flags,outFrame) ) 

#define IDeckLinkOutput_v7_1_DisplayVideoFrameSync(This,theFrame)	\
    ( (This)->lpVtbl -> DisplayVideoFrameSync(This,theFrame) ) 

#define IDeckLinkOutput_v7_1_ScheduleVideoFrame(This,theFrame,displayTime,displayDuration,timeScale)	\
    ( (This)->lpVtbl -> ScheduleVideoFrame(This,theFrame,displayTime,displayDuration,timeScale) ) 

#define IDeckLinkOutput_v7_1_SetScheduledFrameCompletionCallback(This,theCallback)	\
    ( (This)->lpVtbl -> SetScheduledFrameCompletionCallback(This,theCallback) ) 

#define IDeckLinkOutput_v7_1_EnableAudioOutput(This,sampleRate,sampleType,channelCount)	\
    ( (This)->lpVtbl -> EnableAudioOutput(This,sampleRate,sampleType,channelCount) ) 

#define IDeckLinkOutput_v7_1_DisableAudioOutput(This)	\
    ( (This)->lpVtbl -> DisableAudioOutput(This) ) 

#define IDeckLinkOutput_v7_1_WriteAudioSamplesSync(This,buffer,sampleFrameCount,sampleFramesWritten)	\
    ( (This)->lpVtbl -> WriteAudioSamplesSync(This,buffer,sampleFrameCount,sampleFramesWritten) ) 

#define IDeckLinkOutput_v7_1_BeginAudioPreroll(This)	\
    ( (This)->lpVtbl -> BeginAudioPreroll(This) ) 

#define IDeckLinkOutput_v7_1_EndAudioPreroll(This)	\
    ( (This)->lpVtbl -> EndAudioPreroll(This) ) 

#define IDeckLinkOutput_v7_1_ScheduleAudioSamples(This,buffer,sampleFrameCount,streamTime,timeScale,sampleFramesWritten)	\
    ( (This)->lpVtbl -> ScheduleAudioSamples(This,buffer,sampleFrameCount,streamTime,timeScale,sampleFramesWritten) ) 

#define IDeckLinkOutput_v7_1_GetBufferedAudioSampleFrameCount(This,bufferedSampleCount)	\
    ( (This)->lpVtbl -> GetBufferedAudioSampleFrameCount(This,bufferedSampleCount) ) 

#define IDeckLinkOutput_v7_1_FlushBufferedAudioSamples(This)	\
    ( (This)->lpVtbl -> FlushBufferedAudioSamples(This) ) 

#define IDeckLinkOutput_v7_1_SetAudioCallback(This,theCallback)	\
    ( (This)->lpVtbl -> SetAudioCallback(This,theCallback) ) 

#define IDeckLinkOutput_v7_1_StartScheduledPlayback(This,playbackStartTime,timeScale,playbackSpeed)	\
    ( (This)->lpVtbl -> StartScheduledPlayback(This,playbackStartTime,timeScale,playbackSpeed) ) 

#define IDeckLinkOutput_v7_1_StopScheduledPlayback(This,stopPlaybackAtTime,actualStopTime,timeScale)	\
    ( (This)->lpVtbl -> StopScheduledPlayback(This,stopPlaybackAtTime,actualStopTime,timeScale) ) 

#define IDeckLinkOutput_v7_1_GetHardwareReferenceClock(This,desiredTimeScale,elapsedTimeSinceSchedulerBegan)	\
    ( (This)->lpVtbl -> GetHardwareReferenceClock(This,desiredTimeScale,elapsedTimeSinceSchedulerBegan) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkOutput_v7_1_INTERFACE_DEFINED__ */


#ifndef __IDeckLinkInput_v7_1_INTERFACE_DEFINED__
#define __IDeckLinkInput_v7_1_INTERFACE_DEFINED__

/* interface IDeckLinkInput_v7_1 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IDeckLinkInput_v7_1;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("2B54EDEF-5B32-429F-BA11-BB990596EACD")
    IDeckLinkInput_v7_1 : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE DoesSupportVideoMode( 
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            /* [out] */ BMDDisplayModeSupport *result) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetDisplayModeIterator( 
            /* [out] */ IDeckLinkDisplayModeIterator_v7_1 **iterator) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableVideoInput( 
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            BMDVideoInputFlags flags) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableVideoInput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE EnableAudioInput( 
            BMDAudioSampleRate sampleRate,
            BMDAudioSampleType sampleType,
            unsigned long channelCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE DisableAudioInput( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE ReadAudioSamples( 
            void *buffer,
            unsigned long sampleFrameCount,
            /* [out] */ unsigned long *sampleFramesRead,
            /* [out] */ BMDTimeValue *audioPacketTime,
            BMDTimeScale timeScale) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE GetBufferedAudioSampleFrameCount( 
            /* [out] */ unsigned long *bufferedSampleCount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StartStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE StopStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE PauseStreams( void) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE SetCallback( 
            /* [in] */ IDeckLinkInputCallback_v7_1 *theCallback) = 0;
        
    };
    
#else 	/* C style interface */

    typedef struct IDeckLinkInput_v7_1Vtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IDeckLinkInput_v7_1 * This,
            /* [in] */ REFIID riid,
            /* [iid_is][out] */ 
            __RPC__deref_out  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IDeckLinkInput_v7_1 * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IDeckLinkInput_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *DoesSupportVideoMode )( 
            IDeckLinkInput_v7_1 * This,
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            /* [out] */ BMDDisplayModeSupport *result);
        
        HRESULT ( STDMETHODCALLTYPE *GetDisplayModeIterator )( 
            IDeckLinkInput_v7_1 * This,
            /* [out] */ IDeckLinkDisplayModeIterator_v7_1 **iterator);
        
        HRESULT ( STDMETHODCALLTYPE *EnableVideoInput )( 
            IDeckLinkInput_v7_1 * This,
            BMDDisplayMode displayMode,
            BMDPixelFormat pixelFormat,
            BMDVideoInputFlags flags);
        
        HRESULT ( STDMETHODCALLTYPE *DisableVideoInput )( 
            IDeckLinkInput_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *EnableAudioInput )( 
            IDeckLinkInput_v7_1 * This,
            BMDAudioSampleRate sampleRate,
            BMDAudioSampleType sampleType,
            unsigned long channelCount);
        
        HRESULT ( STDMETHODCALLTYPE *DisableAudioInput )( 
            IDeckLinkInput_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *ReadAudioSamples )( 
            IDeckLinkInput_v7_1 * This,
            void *buffer,
            unsigned long sampleFrameCount,
            /* [out] */ unsigned long *sampleFramesRead,
            /* [out] */ BMDTimeValue *audioPacketTime,
            BMDTimeScale timeScale);
        
        HRESULT ( STDMETHODCALLTYPE *GetBufferedAudioSampleFrameCount )( 
            IDeckLinkInput_v7_1 * This,
            /* [out] */ unsigned long *bufferedSampleCount);
        
        HRESULT ( STDMETHODCALLTYPE *StartStreams )( 
            IDeckLinkInput_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *StopStreams )( 
            IDeckLinkInput_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *PauseStreams )( 
            IDeckLinkInput_v7_1 * This);
        
        HRESULT ( STDMETHODCALLTYPE *SetCallback )( 
            IDeckLinkInput_v7_1 * This,
            /* [in] */ IDeckLinkInputCallback_v7_1 *theCallback);
        
        END_INTERFACE
    } IDeckLinkInput_v7_1Vtbl;

    interface IDeckLinkInput_v7_1
    {
        CONST_VTBL struct IDeckLinkInput_v7_1Vtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IDeckLinkInput_v7_1_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IDeckLinkInput_v7_1_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IDeckLinkInput_v7_1_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#define IDeckLinkInput_v7_1_DoesSupportVideoMode(This,displayMode,pixelFormat,result)	\
    ( (This)->lpVtbl -> DoesSupportVideoMode(This,displayMode,pixelFormat,result) ) 

#define IDeckLinkInput_v7_1_GetDisplayModeIterator(This,iterator)	\
    ( (This)->lpVtbl -> GetDisplayModeIterator(This,iterator) ) 

#define IDeckLinkInput_v7_1_EnableVideoInput(This,displayMode,pixelFormat,flags)	\
    ( (This)->lpVtbl -> EnableVideoInput(This,displayMode,pixelFormat,flags) ) 

#define IDeckLinkInput_v7_1_DisableVideoInput(This)	\
    ( (This)->lpVtbl -> DisableVideoInput(This) ) 

#define IDeckLinkInput_v7_1_EnableAudioInput(This,sampleRate,sampleType,channelCount)	\
    ( (This)->lpVtbl -> EnableAudioInput(This,sampleRate,sampleType,channelCount) ) 

#define IDeckLinkInput_v7_1_DisableAudioInput(This)	\
    ( (This)->lpVtbl -> DisableAudioInput(This) ) 

#define IDeckLinkInput_v7_1_ReadAudioSamples(This,buffer,sampleFrameCount,sampleFramesRead,audioPacketTime,timeScale)	\
    ( (This)->lpVtbl -> ReadAudioSamples(This,buffer,sampleFrameCount,sampleFramesRead,audioPacketTime,timeScale) ) 

#define IDeckLinkInput_v7_1_GetBufferedAudioSampleFrameCount(This,bufferedSampleCount)	\
    ( (This)->lpVtbl -> GetBufferedAudioSampleFrameCount(This,bufferedSampleCount) ) 

#define IDeckLinkInput_v7_1_StartStreams(This)	\
    ( (This)->lpVtbl -> StartStreams(This) ) 

#define IDeckLinkInput_v7_1_StopStreams(This)	\
    ( (This)->lpVtbl -> StopStreams(This) ) 

#define IDeckLinkInput_v7_1_PauseStreams(This)	\
    ( (This)->lpVtbl -> PauseStreams(This) ) 

#define IDeckLinkInput_v7_1_SetCallback(This,theCallback)	\
    ( (This)->lpVtbl -> SetCallback(This,theCallback) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IDeckLinkInput_v7_1_INTERFACE_DEFINED__ */

#endif /* __DeckLinkAPI_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


