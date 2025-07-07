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
#include <gst/hip/hip-prelude.h>
#include <gst/hip/gsthip-enums.h>

G_BEGIN_DECLS

GST_HIP_API
hipError_t HipInit (GstHipVendor vendor,
                    unsigned int flags);

GST_HIP_API
hipError_t HipDriverGetVersion (GstHipVendor vendor,
                                int* driverVersion);

GST_HIP_API
hipError_t HipRuntimeGetVersion (GstHipVendor vendor,
                                 int* runtimeVersion);

GST_HIP_API
const char* HipGetErrorName (GstHipVendor vendor,
                             hipError_t hip_error);

GST_HIP_API
const char* HipGetErrorString (GstHipVendor vendor,
                               hipError_t hipError);

GST_HIP_API
hipError_t HipGetDeviceCount (GstHipVendor vendor,
                              int* count);

GST_HIP_API
hipError_t HipGetDeviceProperties (GstHipVendor vendor,
                                   hipDeviceProp_t* prop,
                                   int deviceId);

GST_HIP_API
hipError_t HipDeviceGetAttribute (GstHipVendor vendor,
                                  int* pi,
                                  hipDeviceAttribute_t attr,
                                  int deviceId);

GST_HIP_API
hipError_t HipSetDevice (GstHipVendor vendor,
                         int deviceId);

GST_HIP_API
hipError_t HipMalloc (GstHipVendor vendor,
                      void** ptr,
                      size_t size);

GST_HIP_API
hipError_t HipFree (GstHipVendor vendor,
                    void* ptr);

GST_HIP_API
hipError_t HipHostMalloc (GstHipVendor vendor,
                          void** ptr,
                          size_t size,
                          unsigned int flags);

GST_HIP_API
hipError_t HipHostFree (GstHipVendor vendor,
                        void* ptr);

GST_HIP_API
hipError_t HipStreamCreate (GstHipVendor vendor,
                            hipStream_t* stream);

GST_HIP_API
hipError_t HipStreamDestroy (GstHipVendor vendor,
                             hipStream_t stream);

GST_HIP_API
hipError_t HipStreamSynchronize (GstHipVendor vendor,
                                 hipStream_t stream);

GST_HIP_API
hipError_t HipEventCreateWithFlags (GstHipVendor vendor,
                                    hipEvent_t* event,
                                    unsigned flags);

GST_HIP_API
hipError_t HipEventRecord (GstHipVendor vendor,
                           hipEvent_t event,
                           hipStream_t stream);

GST_HIP_API
hipError_t HipEventDestroy (GstHipVendor vendor,
                            hipEvent_t event);

GST_HIP_API
hipError_t HipEventSynchronize (GstHipVendor vendor,
                                hipEvent_t event);

GST_HIP_API
hipError_t HipEventQuery (GstHipVendor vendor,
                          hipEvent_t event);

GST_HIP_API
hipError_t HipModuleLoadData (GstHipVendor vendor,
                              hipModule_t* module,
                              const void* image);

GST_HIP_API
hipError_t HipModuleUnload (GstHipVendor vendor,
                            hipModule_t module);

GST_HIP_API
hipError_t HipModuleGetFunction (GstHipVendor vendor,
                                 hipFunction_t* function,
                                 hipModule_t module,
                                 const char* kname);

GST_HIP_API
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

GST_HIP_API
hipError_t HipMemcpyParam2DAsync (GstHipVendor vendor,
                                  const hip_Memcpy2D* pCopy,
                                  hipStream_t stream);

GST_HIP_API
hipError_t HipMemsetD8Async (GstHipVendor vendor,
                             hipDeviceptr_t dest,
                             unsigned char value,
                             size_t count,
                             hipStream_t stream);

GST_HIP_API
hipError_t HipMemsetD16Async (GstHipVendor vendor,
                              hipDeviceptr_t dest,
                              unsigned short value,
                              size_t count,
                              hipStream_t stream);

GST_HIP_API
hipError_t HipMemsetD32Async (GstHipVendor vendor,
                              hipDeviceptr_t dst,
                              int value,
                              size_t count,
                              hipStream_t stream);

GST_HIP_API
hipError_t HipTexObjectCreate (GstHipVendor vendor,
                               hipTextureObject_t* pTexObject,
                               const HIP_RESOURCE_DESC* pResDesc,
                               const HIP_TEXTURE_DESC* pTexDesc,
                               const HIP_RESOURCE_VIEW_DESC* pResViewDesc);

GST_HIP_API
hipError_t HipTexObjectDestroy (GstHipVendor vendor,
                                hipTextureObject_t texObject);

GST_HIP_API
hipError_t HipGraphicsMapResources (GstHipVendor vendor,
                                    int count,
                                    hipGraphicsResource_t* resources,
                                    hipStream_t stream);

GST_HIP_API
hipError_t HipGraphicsResourceGetMappedPointer (GstHipVendor vendor,
                                                void** devPtr,
                                                size_t* size,
                                                hipGraphicsResource_t resource);

GST_HIP_API
hipError_t HipGraphicsUnmapResources (GstHipVendor vendor,
                                      int count,
                                      hipGraphicsResource_t* resources,
                                      hipStream_t stream);

GST_HIP_API
hipError_t HipGraphicsUnregisterResource (GstHipVendor vendor,
                                          hipGraphicsResource_t resource);

G_END_DECLS


