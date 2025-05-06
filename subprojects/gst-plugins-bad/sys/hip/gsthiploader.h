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
#include "gsthip-enums.h"

G_BEGIN_DECLS

gboolean gst_hip_load_library (GstHipVendor vendor);

hipError_t HipInit (GstHipVendor vendor,
                    unsigned int flags);

hipError_t HipDriverGetVersion (GstHipVendor vendor,
                                int* driverVersion);

hipError_t HipRuntimeGetVersion (GstHipVendor vendor,
                                 int* runtimeVersion);

const char* HipGetErrorName (GstHipVendor vendor,
                             hipError_t hip_error);

const char* HipGetErrorString (GstHipVendor vendor,
                               hipError_t hipError);

hipError_t HipGetDeviceCount (GstHipVendor vendor,
                              int* count);

hipError_t HipGetDeviceProperties (GstHipVendor vendor,
                                   hipDeviceProp_t* prop,
                                   int deviceId);

hipError_t HipDeviceGetAttribute (GstHipVendor vendor,
                                  int* pi,
                                  hipDeviceAttribute_t attr,
                                  int deviceId);

hipError_t HipSetDevice (GstHipVendor vendor,
                         int deviceId);

hipError_t HipMalloc (GstHipVendor vendor,
                      void** ptr,
                      size_t size);

hipError_t HipFree (GstHipVendor vendor,
                    void* ptr);

hipError_t HipHostMalloc (GstHipVendor vendor,
                          void** ptr,
                          size_t size,
                          unsigned int flags);

hipError_t HipHostFree (GstHipVendor vendor,
                        void* ptr);

hipError_t HipStreamSynchronize (GstHipVendor vendor,
                                 hipStream_t stream);

hipError_t HipModuleLoadData (GstHipVendor vendor,
                              hipModule_t* module,
                              const void* image);

hipError_t HipModuleUnload (GstHipVendor vendor,
                            hipModule_t module);

hipError_t HipModuleGetFunction (GstHipVendor vendor,
                                 hipFunction_t* function,
                                 hipModule_t module,
                                 const char* kname);

hipError_t HipModuleLaunchKernel (GstHipVendor vendor,
                                  hipFunction_t f,
                                  unsigned int gridDimX,
                                  unsigned int gridDimY,
                                  unsigned int gridDimZ,
                                  unsigned int blockDimX,
                                  unsigned int blockDimY,
                                  unsigned int blockDimZ,
                                  unsigned int sharedMemBytes,
                                  hipStream_t stream,
                                  void** kernelParams,
                                  void** extra);

hipError_t HipMemcpyParam2DAsync (GstHipVendor vendor,
                                  const hip_Memcpy2D* pCopy,
                                  hipStream_t stream);

hipError_t HipTexObjectCreate (GstHipVendor vendor,
                               hipTextureObject_t* pTexObject,
                               const HIP_RESOURCE_DESC* pResDesc,
                               const HIP_TEXTURE_DESC* pTexDesc,
                               const HIP_RESOURCE_VIEW_DESC* pResViewDesc);

hipError_t HipTexObjectDestroy (GstHipVendor vendor,
                                hipTextureObject_t texObject);

G_END_DECLS


