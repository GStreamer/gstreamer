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
#include <hip/hip_runtime.h>

G_BEGIN_DECLS

gboolean gst_hip_load_library (void);

const char* HipGetErrorName(hipError_t hip_error);

const char* HipGetErrorString(hipError_t hipError);

hipError_t HipInit(unsigned int flags);

hipError_t HipGetDeviceCount(int* count);

hipError_t HipGetDeviceProperties(hipDeviceProp_t* prop, int deviceId);

hipError_t HipDeviceGetAttribute(int* pi, hipDeviceAttribute_t attr, int deviceId);

hipError_t HipSetDevice(int deviceId);

hipError_t HipMalloc(void** ptr, size_t size);

hipError_t HipFree(void* ptr);

hipError_t HipHostMalloc(void** ptr, size_t size, unsigned int flags);

hipError_t HipHostFree(void* ptr);

hipError_t HipStreamSynchronize(hipStream_t stream);

hipError_t HipModuleLoadData(hipModule_t* module, const void* image);

hipError_t HipModuleUnload(hipModule_t module);

hipError_t HipModuleGetFunction(hipFunction_t* function, hipModule_t module, const char* kname);

hipError_t HipModuleLaunchKernel(hipFunction_t f, unsigned int gridDimX, unsigned int gridDimY,
    unsigned int gridDimZ, unsigned int blockDimX,
    unsigned int blockDimY, unsigned int blockDimZ,
    unsigned int sharedMemBytes, hipStream_t stream,
    void** kernelParams, void** extra);

hipError_t HipMemcpyParam2DAsync(const hip_Memcpy2D* pCopy, hipStream_t stream);

hipError_t HipTexObjectCreate(
    hipTextureObject_t* pTexObject,
    const HIP_RESOURCE_DESC* pResDesc,
    const HIP_TEXTURE_DESC* pTexDesc,
    const HIP_RESOURCE_VIEW_DESC* pResViewDesc);

hipError_t HipTexObjectDestroy(
    hipTextureObject_t texObject);

G_END_DECLS


