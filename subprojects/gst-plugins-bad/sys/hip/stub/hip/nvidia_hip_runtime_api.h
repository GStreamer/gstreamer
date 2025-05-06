/*
Copyright (c) 2015 - 2022 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#pragma once

#include <hip/hip_runtime_api.h>
#include <cuda.h>
#include <driver_types.h>

inline static hipError_t hipCUDAErrorTohipError(cudaError_t cuError) {
    switch (cuError) {
        case cudaSuccess:
            return hipSuccess;
        case cudaErrorProfilerDisabled:
            return hipErrorProfilerDisabled;
        case cudaErrorProfilerNotInitialized:
            return hipErrorProfilerNotInitialized;
        case cudaErrorProfilerAlreadyStarted:
            return hipErrorProfilerAlreadyStarted;
        case cudaErrorProfilerAlreadyStopped:
            return hipErrorProfilerAlreadyStopped;
        case cudaErrorInsufficientDriver:
            return hipErrorInsufficientDriver;
        case cudaErrorUnsupportedLimit:
            return hipErrorUnsupportedLimit;
        case cudaErrorPeerAccessUnsupported:
            return hipErrorPeerAccessUnsupported;
        case cudaErrorInvalidGraphicsContext:
            return hipErrorInvalidGraphicsContext;
        case cudaErrorSharedObjectSymbolNotFound:
            return hipErrorSharedObjectSymbolNotFound;
        case cudaErrorSharedObjectInitFailed:
            return hipErrorSharedObjectInitFailed;
        case cudaErrorOperatingSystem:
            return hipErrorOperatingSystem;
        case cudaErrorIllegalState:
            return hipErrorIllegalState;
        case cudaErrorSetOnActiveProcess:
            return hipErrorSetOnActiveProcess;
        case cudaErrorIllegalAddress:
            return hipErrorIllegalAddress;
        case cudaErrorInvalidSymbol:
            return hipErrorInvalidSymbol;
        case cudaErrorMissingConfiguration:
            return hipErrorMissingConfiguration;
        case cudaErrorMemoryAllocation:
            return hipErrorOutOfMemory;
        case cudaErrorInitializationError:
            return hipErrorNotInitialized;
        case cudaErrorLaunchFailure:
            return hipErrorLaunchFailure;
        case cudaErrorCooperativeLaunchTooLarge:
            return hipErrorCooperativeLaunchTooLarge;
        case cudaErrorPriorLaunchFailure:
            return hipErrorPriorLaunchFailure;
        case cudaErrorLaunchOutOfResources:
            return hipErrorLaunchOutOfResources;
        case cudaErrorInvalidDeviceFunction:
            return hipErrorInvalidDeviceFunction;
        case cudaErrorInvalidConfiguration:
            return hipErrorInvalidConfiguration;
        case cudaErrorInvalidDevice:
            return hipErrorInvalidDevice;
        case cudaErrorInvalidValue:
            return hipErrorInvalidValue;
        case cudaErrorInvalidPitchValue:
            return hipErrorInvalidPitchValue;
        case cudaErrorInvalidDevicePointer:
            return hipErrorInvalidDevicePointer;
        case cudaErrorInvalidMemcpyDirection:
            return hipErrorInvalidMemcpyDirection;
        case cudaErrorInvalidResourceHandle:
            return hipErrorInvalidHandle;
        case cudaErrorNotReady:
            return hipErrorNotReady;
        case cudaErrorNoDevice:
            return hipErrorNoDevice;
        case cudaErrorPeerAccessAlreadyEnabled:
            return hipErrorPeerAccessAlreadyEnabled;
        case cudaErrorPeerAccessNotEnabled:
            return hipErrorPeerAccessNotEnabled;
        case cudaErrorContextIsDestroyed:
            return hipErrorContextIsDestroyed;
        case cudaErrorHostMemoryAlreadyRegistered:
            return hipErrorHostMemoryAlreadyRegistered;
        case cudaErrorHostMemoryNotRegistered:
            return hipErrorHostMemoryNotRegistered;
        case cudaErrorMapBufferObjectFailed:
            return hipErrorMapFailed;
        case cudaErrorAssert:
            return hipErrorAssert;
        case cudaErrorNotSupported:
            return hipErrorNotSupported;
        case cudaErrorCudartUnloading:
            return hipErrorDeinitialized;
        case cudaErrorInvalidKernelImage:
            return hipErrorInvalidImage;
        case cudaErrorUnmapBufferObjectFailed:
            return hipErrorUnmapFailed;
        case cudaErrorNoKernelImageForDevice:
            return hipErrorNoBinaryForGpu;
        case cudaErrorECCUncorrectable:
            return hipErrorECCNotCorrectable;
        case cudaErrorDeviceAlreadyInUse:
            return hipErrorContextAlreadyInUse;
        case cudaErrorInvalidPtx:
            return hipErrorInvalidKernelFile;
        case cudaErrorLaunchTimeout:
            return hipErrorLaunchTimeOut;
        case cudaErrorInvalidSource:
            return hipErrorInvalidSource;
        case cudaErrorFileNotFound:
            return hipErrorFileNotFound;
        case cudaErrorSymbolNotFound:
            return hipErrorNotFound;
        case cudaErrorArrayIsMapped:
            return hipErrorArrayIsMapped;
        case cudaErrorNotMappedAsPointer:
            return hipErrorNotMappedAsPointer;
        case cudaErrorNotMappedAsArray:
            return hipErrorNotMappedAsArray;
        case cudaErrorNotMapped:
            return hipErrorNotMapped;
        case cudaErrorAlreadyAcquired:
            return hipErrorAlreadyAcquired;
        case cudaErrorAlreadyMapped:
            return hipErrorAlreadyMapped;
        case cudaErrorDeviceUninitialized:
            return hipErrorInvalidContext;
        case cudaErrorStreamCaptureUnsupported:
            return hipErrorStreamCaptureUnsupported;
        case cudaErrorStreamCaptureInvalidated:
            return hipErrorStreamCaptureInvalidated;
        case cudaErrorStreamCaptureMerge:
            return hipErrorStreamCaptureMerge;
        case cudaErrorStreamCaptureUnmatched:
            return hipErrorStreamCaptureUnmatched;
        case cudaErrorStreamCaptureUnjoined:
            return hipErrorStreamCaptureUnjoined;
        case cudaErrorStreamCaptureIsolation:
            return hipErrorStreamCaptureIsolation;
        case cudaErrorStreamCaptureImplicit:
            return hipErrorStreamCaptureImplicit;
        case cudaErrorCapturedEvent:
            return hipErrorCapturedEvent;
        case cudaErrorStreamCaptureWrongThread:
            return hipErrorStreamCaptureWrongThread;
        case cudaErrorGraphExecUpdateFailure:
            return hipErrorGraphExecUpdateFailure;
        case cudaErrorUnknown:
        default:
            return hipErrorUnknown;  // Note - translated error.
    }
}

inline static hipError_t hipCUResultTohipError(CUresult cuError) {
    switch (cuError) {
        case CUDA_SUCCESS:
            return hipSuccess;
        case CUDA_ERROR_OUT_OF_MEMORY:
            return hipErrorOutOfMemory;
        case CUDA_ERROR_INVALID_VALUE:
            return hipErrorInvalidValue;
        case CUDA_ERROR_INVALID_DEVICE:
            return hipErrorInvalidDevice;
        case CUDA_ERROR_DEINITIALIZED:
            return hipErrorDeinitialized;
        case CUDA_ERROR_NO_DEVICE:
            return hipErrorNoDevice;
        case CUDA_ERROR_INVALID_CONTEXT:
            return hipErrorInvalidContext;
        case CUDA_ERROR_NOT_INITIALIZED:
            return hipErrorNotInitialized;
        case CUDA_ERROR_INVALID_HANDLE:
            return hipErrorInvalidHandle;
        case CUDA_ERROR_MAP_FAILED:
            return hipErrorMapFailed;
        case CUDA_ERROR_PROFILER_DISABLED:
            return hipErrorProfilerDisabled;
        case CUDA_ERROR_PROFILER_NOT_INITIALIZED:
            return hipErrorProfilerNotInitialized;
        case CUDA_ERROR_PROFILER_ALREADY_STARTED:
            return hipErrorProfilerAlreadyStarted;
        case CUDA_ERROR_PROFILER_ALREADY_STOPPED:
            return hipErrorProfilerAlreadyStopped;
        case CUDA_ERROR_INVALID_IMAGE:
            return hipErrorInvalidImage;
        case CUDA_ERROR_CONTEXT_ALREADY_CURRENT:
            return hipErrorContextAlreadyCurrent;
        case CUDA_ERROR_UNMAP_FAILED:
            return hipErrorUnmapFailed;
        case CUDA_ERROR_ARRAY_IS_MAPPED:
            return hipErrorArrayIsMapped;
        case CUDA_ERROR_ALREADY_MAPPED:
            return hipErrorAlreadyMapped;
        case CUDA_ERROR_NO_BINARY_FOR_GPU:
            return hipErrorNoBinaryForGpu;
        case CUDA_ERROR_ALREADY_ACQUIRED:
            return hipErrorAlreadyAcquired;
        case CUDA_ERROR_NOT_MAPPED:
            return hipErrorNotMapped;
        case CUDA_ERROR_NOT_MAPPED_AS_ARRAY:
            return hipErrorNotMappedAsArray;
        case CUDA_ERROR_NOT_MAPPED_AS_POINTER:
            return hipErrorNotMappedAsPointer;
        case CUDA_ERROR_ECC_UNCORRECTABLE:
            return hipErrorECCNotCorrectable;
        case CUDA_ERROR_UNSUPPORTED_LIMIT:
            return hipErrorUnsupportedLimit;
        case CUDA_ERROR_CONTEXT_ALREADY_IN_USE:
            return hipErrorContextAlreadyInUse;
        case CUDA_ERROR_PEER_ACCESS_UNSUPPORTED:
            return hipErrorPeerAccessUnsupported;
        case CUDA_ERROR_INVALID_PTX:
            return hipErrorInvalidKernelFile;
        case CUDA_ERROR_INVALID_GRAPHICS_CONTEXT:
            return hipErrorInvalidGraphicsContext;
        case CUDA_ERROR_INVALID_SOURCE:
            return hipErrorInvalidSource;
        case CUDA_ERROR_FILE_NOT_FOUND:
            return hipErrorFileNotFound;
        case CUDA_ERROR_SHARED_OBJECT_SYMBOL_NOT_FOUND:
            return hipErrorSharedObjectSymbolNotFound;
        case CUDA_ERROR_SHARED_OBJECT_INIT_FAILED:
            return hipErrorSharedObjectInitFailed;
        case CUDA_ERROR_OPERATING_SYSTEM:
            return hipErrorOperatingSystem;
        case CUDA_ERROR_ILLEGAL_STATE:
            return hipErrorIllegalState;
        case CUDA_ERROR_NOT_FOUND:
            return hipErrorNotFound;
        case CUDA_ERROR_NOT_READY:
            return hipErrorNotReady;
        case CUDA_ERROR_ILLEGAL_ADDRESS:
            return hipErrorIllegalAddress;
        case CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES:
            return hipErrorLaunchOutOfResources;
        case CUDA_ERROR_LAUNCH_TIMEOUT:
            return hipErrorLaunchTimeOut;
        case CUDA_ERROR_PEER_ACCESS_ALREADY_ENABLED:
            return hipErrorPeerAccessAlreadyEnabled;
        case CUDA_ERROR_PEER_ACCESS_NOT_ENABLED:
            return hipErrorPeerAccessNotEnabled;
        case CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE:
            return hipErrorSetOnActiveProcess;
        case CUDA_ERROR_CONTEXT_IS_DESTROYED:
            return hipErrorContextIsDestroyed;
        case CUDA_ERROR_ASSERT:
            return hipErrorAssert;
        case CUDA_ERROR_HOST_MEMORY_ALREADY_REGISTERED:
            return hipErrorHostMemoryAlreadyRegistered;
        case CUDA_ERROR_HOST_MEMORY_NOT_REGISTERED:
            return hipErrorHostMemoryNotRegistered;
        case CUDA_ERROR_LAUNCH_FAILED:
            return hipErrorLaunchFailure;
        case CUDA_ERROR_COOPERATIVE_LAUNCH_TOO_LARGE:
            return hipErrorCooperativeLaunchTooLarge;
        case CUDA_ERROR_NOT_SUPPORTED:
            return hipErrorNotSupported;
        case CUDA_ERROR_STREAM_CAPTURE_UNSUPPORTED:
            return hipErrorStreamCaptureUnsupported;
        case CUDA_ERROR_STREAM_CAPTURE_INVALIDATED:
            return hipErrorStreamCaptureInvalidated;
        case CUDA_ERROR_STREAM_CAPTURE_MERGE:
            return hipErrorStreamCaptureMerge;
        case CUDA_ERROR_STREAM_CAPTURE_UNMATCHED:
            return hipErrorStreamCaptureUnmatched;
        case CUDA_ERROR_STREAM_CAPTURE_UNJOINED:
            return hipErrorStreamCaptureUnjoined;
        case CUDA_ERROR_STREAM_CAPTURE_ISOLATION:
            return hipErrorStreamCaptureIsolation;
        case CUDA_ERROR_STREAM_CAPTURE_IMPLICIT:
            return hipErrorStreamCaptureImplicit;
        case CUDA_ERROR_CAPTURED_EVENT:
            return hipErrorCapturedEvent;
        case CUDA_ERROR_STREAM_CAPTURE_WRONG_THREAD:
            return hipErrorStreamCaptureWrongThread;
        case CUDA_ERROR_GRAPH_EXEC_UPDATE_FAILURE:
            return hipErrorGraphExecUpdateFailure;
        case CUDA_ERROR_UNKNOWN:
        default:
            return hipErrorUnknown;  // Note - translated error.
    }
}

inline static CUresult hipErrorToCUResult(hipError_t hError) {
    switch (hError) {
        case hipSuccess:
            return CUDA_SUCCESS;
        case hipErrorOutOfMemory:
            return CUDA_ERROR_OUT_OF_MEMORY;
        case hipErrorInvalidValue:
            return CUDA_ERROR_INVALID_VALUE;
        case hipErrorInvalidDevice:
            return CUDA_ERROR_INVALID_DEVICE;
        case hipErrorDeinitialized:
            return CUDA_ERROR_DEINITIALIZED;
        case hipErrorNoDevice:
            return CUDA_ERROR_NO_DEVICE;
        case hipErrorInvalidContext:
            return CUDA_ERROR_INVALID_CONTEXT;
        case hipErrorNotInitialized:
            return CUDA_ERROR_NOT_INITIALIZED;
        case hipErrorInvalidHandle:
            return CUDA_ERROR_INVALID_HANDLE;
        case hipErrorMapFailed:
            return CUDA_ERROR_MAP_FAILED;
        case hipErrorProfilerDisabled:
            return CUDA_ERROR_PROFILER_DISABLED;
        case hipErrorProfilerNotInitialized:
            return CUDA_ERROR_PROFILER_NOT_INITIALIZED;
        case hipErrorProfilerAlreadyStarted:
            return CUDA_ERROR_PROFILER_ALREADY_STARTED;
        case hipErrorProfilerAlreadyStopped:
            return CUDA_ERROR_PROFILER_ALREADY_STOPPED;
        case hipErrorInvalidImage:
            return CUDA_ERROR_INVALID_IMAGE;
        case hipErrorContextAlreadyCurrent:
            return CUDA_ERROR_CONTEXT_ALREADY_CURRENT;
        case hipErrorUnmapFailed:
            return CUDA_ERROR_UNMAP_FAILED;
        case hipErrorArrayIsMapped:
            return CUDA_ERROR_ARRAY_IS_MAPPED;
        case hipErrorAlreadyMapped:
            return CUDA_ERROR_ALREADY_MAPPED;
        case hipErrorNoBinaryForGpu:
            return CUDA_ERROR_NO_BINARY_FOR_GPU;
        case hipErrorAlreadyAcquired:
            return CUDA_ERROR_ALREADY_ACQUIRED;
        case hipErrorNotMapped:
            return CUDA_ERROR_NOT_MAPPED;
        case hipErrorNotMappedAsArray:
            return CUDA_ERROR_NOT_MAPPED_AS_ARRAY;
        case hipErrorNotMappedAsPointer:
            return CUDA_ERROR_NOT_MAPPED_AS_POINTER;
        case hipErrorECCNotCorrectable:
            return CUDA_ERROR_ECC_UNCORRECTABLE;
        case hipErrorUnsupportedLimit:
            return CUDA_ERROR_UNSUPPORTED_LIMIT;
        case hipErrorContextAlreadyInUse:
            return CUDA_ERROR_CONTEXT_ALREADY_IN_USE;
        case hipErrorPeerAccessUnsupported:
            return CUDA_ERROR_PEER_ACCESS_UNSUPPORTED;
        case hipErrorInvalidKernelFile:
            return CUDA_ERROR_INVALID_PTX;
        case hipErrorInvalidGraphicsContext:
            return CUDA_ERROR_INVALID_GRAPHICS_CONTEXT;
        case hipErrorInvalidSource:
            return CUDA_ERROR_INVALID_SOURCE;
        case hipErrorFileNotFound:
            return CUDA_ERROR_FILE_NOT_FOUND;
        case hipErrorSharedObjectSymbolNotFound:
            return CUDA_ERROR_SHARED_OBJECT_SYMBOL_NOT_FOUND;
        case hipErrorSharedObjectInitFailed:
            return CUDA_ERROR_SHARED_OBJECT_INIT_FAILED;
        case hipErrorOperatingSystem:
            return CUDA_ERROR_OPERATING_SYSTEM;
        case hipErrorIllegalState:
            return CUDA_ERROR_ILLEGAL_STATE;
        case hipErrorNotFound:
            return CUDA_ERROR_NOT_FOUND;
        case hipErrorNotReady:
            return CUDA_ERROR_NOT_READY;
        case hipErrorIllegalAddress:
            return CUDA_ERROR_ILLEGAL_ADDRESS;
        case hipErrorLaunchOutOfResources:
            return CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES;
        case hipErrorLaunchTimeOut:
            return CUDA_ERROR_LAUNCH_TIMEOUT;
        case hipErrorPeerAccessAlreadyEnabled:
            return CUDA_ERROR_PEER_ACCESS_ALREADY_ENABLED;
        case hipErrorPeerAccessNotEnabled:
            return CUDA_ERROR_PEER_ACCESS_NOT_ENABLED;
        case hipErrorSetOnActiveProcess:
            return CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE;
        case hipErrorContextIsDestroyed:
            return CUDA_ERROR_CONTEXT_IS_DESTROYED;
        case hipErrorAssert:
            return CUDA_ERROR_ASSERT;
        case hipErrorHostMemoryAlreadyRegistered:
            return CUDA_ERROR_HOST_MEMORY_ALREADY_REGISTERED;
        case hipErrorHostMemoryNotRegistered:
            return CUDA_ERROR_HOST_MEMORY_NOT_REGISTERED;
        case hipErrorLaunchFailure:
            return CUDA_ERROR_LAUNCH_FAILED;
        case hipErrorCooperativeLaunchTooLarge:
            return CUDA_ERROR_COOPERATIVE_LAUNCH_TOO_LARGE;
        case hipErrorNotSupported:
            return CUDA_ERROR_NOT_SUPPORTED;
        case hipErrorStreamCaptureUnsupported:
            return CUDA_ERROR_STREAM_CAPTURE_UNSUPPORTED;
        case hipErrorStreamCaptureInvalidated:
            return CUDA_ERROR_STREAM_CAPTURE_INVALIDATED;
        case hipErrorStreamCaptureMerge:
            return CUDA_ERROR_STREAM_CAPTURE_MERGE;
        case hipErrorStreamCaptureUnmatched:
            return CUDA_ERROR_STREAM_CAPTURE_UNMATCHED;
        case hipErrorStreamCaptureUnjoined:
            return CUDA_ERROR_STREAM_CAPTURE_UNJOINED;
        case hipErrorStreamCaptureIsolation:
            return CUDA_ERROR_STREAM_CAPTURE_ISOLATION;
        case hipErrorStreamCaptureImplicit:
            return CUDA_ERROR_STREAM_CAPTURE_IMPLICIT;
        case hipErrorCapturedEvent:
            return CUDA_ERROR_CAPTURED_EVENT;
        case hipErrorStreamCaptureWrongThread:
            return CUDA_ERROR_STREAM_CAPTURE_WRONG_THREAD;
        case hipErrorGraphExecUpdateFailure:
            return CUDA_ERROR_GRAPH_EXEC_UPDATE_FAILURE;
        case hipErrorUnknown:
        default:
            return CUDA_ERROR_UNKNOWN;  // Note - translated error.
    }
}

inline static cudaError_t hipErrorToCudaError(hipError_t hError) {
    switch (hError) {
        case hipSuccess:
            return cudaSuccess;
        case hipErrorOutOfMemory:
            return cudaErrorMemoryAllocation;
        case hipErrorProfilerDisabled:
          return cudaErrorProfilerDisabled;
        case hipErrorProfilerNotInitialized:
            return cudaErrorProfilerNotInitialized;
        case hipErrorProfilerAlreadyStarted:
            return cudaErrorProfilerAlreadyStarted;
        case hipErrorProfilerAlreadyStopped:
            return cudaErrorProfilerAlreadyStopped;
        case hipErrorInvalidConfiguration:
            return cudaErrorInvalidConfiguration;
        case hipErrorLaunchOutOfResources:
            return cudaErrorLaunchOutOfResources;
        case hipErrorInvalidValue:
            return cudaErrorInvalidValue;
        case hipErrorInvalidPitchValue:
            return cudaErrorInvalidPitchValue;
        case hipErrorInvalidHandle:
            return cudaErrorInvalidResourceHandle;
        case hipErrorInvalidDevice:
            return cudaErrorInvalidDevice;
        case hipErrorInvalidMemcpyDirection:
            return cudaErrorInvalidMemcpyDirection;
        case hipErrorInvalidDevicePointer:
            return cudaErrorInvalidDevicePointer;
        case hipErrorNotInitialized:
            return cudaErrorInitializationError;
        case hipErrorNoDevice:
            return cudaErrorNoDevice;
        case hipErrorNotReady:
            return cudaErrorNotReady;
        case hipErrorPeerAccessNotEnabled:
            return cudaErrorPeerAccessNotEnabled;
        case hipErrorPeerAccessAlreadyEnabled:
            return cudaErrorPeerAccessAlreadyEnabled;
        case hipErrorHostMemoryAlreadyRegistered:
            return cudaErrorHostMemoryAlreadyRegistered;
        case hipErrorHostMemoryNotRegistered:
            return cudaErrorHostMemoryNotRegistered;
        case hipErrorDeinitialized:
            return cudaErrorCudartUnloading;
        case hipErrorInvalidSymbol:
            return cudaErrorInvalidSymbol;
        case hipErrorInsufficientDriver:
            return cudaErrorInsufficientDriver;
        case hipErrorMissingConfiguration:
            return cudaErrorMissingConfiguration;
        case hipErrorPriorLaunchFailure:
            return cudaErrorPriorLaunchFailure;
        case hipErrorInvalidDeviceFunction:
            return cudaErrorInvalidDeviceFunction;
        case hipErrorInvalidImage:
            return cudaErrorInvalidKernelImage;
        case hipErrorInvalidContext:
            return cudaErrorDeviceUninitialized;
            return cudaErrorUnknown;
        case hipErrorMapFailed:
            return cudaErrorMapBufferObjectFailed;
        case hipErrorUnmapFailed:
            return cudaErrorUnmapBufferObjectFailed;
        case hipErrorArrayIsMapped:
            return cudaErrorArrayIsMapped;
        case hipErrorAlreadyMapped:
            return cudaErrorAlreadyMapped;
        case hipErrorNoBinaryForGpu:
            return cudaErrorNoKernelImageForDevice;
        case hipErrorAlreadyAcquired:
            return cudaErrorAlreadyAcquired;
        case hipErrorNotMapped:
            return cudaErrorNotMapped;
        case hipErrorNotMappedAsArray:
            return cudaErrorNotMappedAsArray;
        case hipErrorNotMappedAsPointer:
            return cudaErrorNotMappedAsPointer;
        case hipErrorECCNotCorrectable:
            return cudaErrorECCUncorrectable;
        case hipErrorUnsupportedLimit:
            return cudaErrorUnsupportedLimit;
        case hipErrorContextAlreadyInUse:
            return cudaErrorDeviceAlreadyInUse;
        case hipErrorPeerAccessUnsupported:
            return cudaErrorPeerAccessUnsupported;
        case hipErrorInvalidKernelFile:
            return cudaErrorInvalidPtx;
        case hipErrorInvalidGraphicsContext:
            return cudaErrorInvalidGraphicsContext;
        case hipErrorInvalidSource:
            return cudaErrorInvalidSource;
        case hipErrorFileNotFound:
            return cudaErrorFileNotFound;
        case hipErrorSharedObjectSymbolNotFound:
            return cudaErrorSharedObjectSymbolNotFound;
        case hipErrorSharedObjectInitFailed:
            return cudaErrorSharedObjectInitFailed;
        case hipErrorOperatingSystem:
            return cudaErrorOperatingSystem;
        case hipErrorIllegalState:
            return cudaErrorIllegalState;
        case hipErrorNotFound:
            return cudaErrorSymbolNotFound;
        case hipErrorIllegalAddress:
            return cudaErrorIllegalAddress;
        case hipErrorLaunchTimeOut:
            return cudaErrorLaunchTimeout;
        case hipErrorSetOnActiveProcess:
            return cudaErrorSetOnActiveProcess;
        case hipErrorContextIsDestroyed:
            return cudaErrorContextIsDestroyed;
        case hipErrorAssert:
            return cudaErrorAssert;
        case hipErrorLaunchFailure:
            return cudaErrorLaunchFailure;
        case hipErrorCooperativeLaunchTooLarge:
            return cudaErrorCooperativeLaunchTooLarge;
        case hipErrorStreamCaptureUnsupported:
            return cudaErrorStreamCaptureUnsupported;
        case hipErrorStreamCaptureInvalidated:
            return cudaErrorStreamCaptureInvalidated;
        case hipErrorStreamCaptureMerge:
            return cudaErrorStreamCaptureMerge;
        case hipErrorStreamCaptureUnmatched:
            return cudaErrorStreamCaptureUnmatched;
        case hipErrorStreamCaptureUnjoined:
            return cudaErrorStreamCaptureUnjoined;
        case hipErrorStreamCaptureIsolation:
            return cudaErrorStreamCaptureIsolation;
        case hipErrorStreamCaptureImplicit:
            return cudaErrorStreamCaptureImplicit;
        case hipErrorCapturedEvent:
            return cudaErrorCapturedEvent;
        case hipErrorStreamCaptureWrongThread:
            return cudaErrorStreamCaptureWrongThread;
        case hipErrorGraphExecUpdateFailure:
            return cudaErrorGraphExecUpdateFailure;
        case hipErrorNotSupported:
            return cudaErrorNotSupported;
        // HSA: does not exist in CUDA
        case hipErrorRuntimeMemory:
        // HSA: does not exist in CUDA
        case hipErrorRuntimeOther:
        case hipErrorUnknown:
        case hipErrorTbd:
        default:
            return cudaErrorUnknown;  // Note - translated error.
    }
}

static inline void hipMemcpy2DTocudaMemcpy2D(CUDA_MEMCPY2D &a, const hip_Memcpy2D* p){
    a.srcXInBytes = (size_t)p->srcXInBytes;
    a.srcY = (size_t)p->srcY;
    switch (p->srcMemoryType) {
        case hipMemoryTypeHost:
            a.srcMemoryType = CU_MEMORYTYPE_HOST;
            break;
        case hipMemoryTypeDevice:
            a.srcMemoryType = CU_MEMORYTYPE_DEVICE;
            break;
        case hipMemoryTypeArray:
            a.srcMemoryType = CU_MEMORYTYPE_ARRAY;
            break;
        default:
            a.srcMemoryType = CU_MEMORYTYPE_UNIFIED;
    }
    a.srcHost = p->srcHost;
    a.srcDevice = (CUdeviceptr)p->srcDevice;
    a.srcArray = (CUarray)p->srcArray;
    a.srcPitch = (size_t)p->srcPitch;
    a.dstXInBytes = (size_t)p->dstXInBytes;
    a.dstY = (size_t)p->dstY;
    switch (p->dstMemoryType) {
        case hipMemoryTypeHost:
            a.dstMemoryType = CU_MEMORYTYPE_HOST;
            break;
        case hipMemoryTypeDevice:
            a.dstMemoryType = CU_MEMORYTYPE_DEVICE;
            break;
        case hipMemoryTypeArray:
            a.dstMemoryType = CU_MEMORYTYPE_ARRAY;
            break;
        default:
            a.dstMemoryType = CU_MEMORYTYPE_UNIFIED;
    }
    a.dstHost = p->dstHost;
    a.dstDevice = (CUdeviceptr)p->dstDevice;
    a.dstArray = (CUarray)p->dstArray;
    a.dstPitch = (size_t)p->dstPitch;
    a.WidthInBytes = (size_t)p->WidthInBytes;
    a.Height = (size_t)p->Height;
}