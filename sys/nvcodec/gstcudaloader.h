/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_CUDA_LOADER_H__
#define __GST_CUDA_LOADER_H__

#include "stub/cuda.h"

#include <gst/gst.h>

G_BEGIN_DECLS

gboolean gst_cuda_load_library (void);

/* cuda.h */
CUresult CUDAAPI CuInit             (unsigned int Flags);

CUresult CUDAAPI CuGetErrorName     (CUresult error,
                                     const char **pStr);

CUresult CUDAAPI CuGetErrorString   (CUresult error,
                                     const char **pStr);

CUresult CUDAAPI CuCtxCreate        (CUcontext * pctx,
                                     unsigned int flags,
                                     CUdevice dev);

CUresult CUDAAPI CuCtxDestroy       (CUcontext ctx);

CUresult CUDAAPI CuCtxPopCurrent    (CUcontext * pctx);

CUresult CUDAAPI CuCtxPushCurrent   (CUcontext ctx);

CUresult CUDAAPI CuCtxEnablePeerAccess (CUcontext peerContext,
                                             unsigned int Flags);

CUresult CUDAAPI CuCtxDisablePeerAccess (CUcontext peerContext);

CUresult CUDAAPI CuGraphicsMapResources     (unsigned int count,
                                             CUgraphicsResource * resources,
                                             CUstream hStream);

CUresult CUDAAPI CuGraphicsUnmapResources   (unsigned int count,
                                             CUgraphicsResource * resources,
                                             CUstream hStream);

CUresult CUDAAPI CuGraphicsSubResourceGetMappedArray    (CUarray * pArray,
                                                         CUgraphicsResource resource,
                                                         unsigned int arrayIndex,
                                                         unsigned int mipLevel);

CUresult CUDAAPI CuGraphicsResourceGetMappedPointer     (CUdeviceptr * pDevPtr,
                                                         size_t * pSize,
                                                         CUgraphicsResource resource);

CUresult CUDAAPI CuGraphicsUnregisterResource           (CUgraphicsResource resource);

CUresult CUDAAPI CuMemAlloc         (CUdeviceptr * dptr,
                                     unsigned int bytesize);

CUresult CUDAAPI CuMemAllocPitch    (CUdeviceptr * dptr,
                                     size_t * pPitch,
                                     size_t WidthInBytes,
                                     size_t Height,
                                     unsigned int ElementSizeBytes);

CUresult CUDAAPI CuMemAllocHost     (void **pp,
                                     unsigned int bytesize);

CUresult CUDAAPI CuMemcpy2D         (const CUDA_MEMCPY2D * pCopy);

CUresult CUDAAPI CuMemcpy2DAsync    (const CUDA_MEMCPY2D *pCopy, CUstream hStream);

CUresult CUDAAPI CuMemFree          (CUdeviceptr dptr);

CUresult CUDAAPI CuMemFreeHost      (void *p);

CUresult CUDAAPI CuStreamCreate     (CUstream *phStream,
                                     unsigned int Flags);

CUresult CUDAAPI CuStreamDestroy    (CUstream hStream);

CUresult CUDAAPI CuStreamSynchronize (CUstream hStream);

CUresult CUDAAPI CuDeviceGet        (CUdevice * device,
                                     int ordinal);

CUresult CUDAAPI CuDeviceGetCount   (int *count);

CUresult CUDAAPI CuDeviceGetName    (char *name,
                                     int len,
                                     CUdevice dev);

CUresult CUDAAPI CuDeviceGetAttribute (int *pi,
                                       CUdevice_attribute attrib,
                                       CUdevice dev);

CUresult CUDAAPI CuDeviceCanAccessPeer (int *canAccessPeer,
                                        CUdevice dev,
                                        CUdevice peerDev);

CUresult CUDAAPI CuDriverGetVersion   (int * driverVersion);

CUresult CUDAAPI CuModuleLoadData     (CUmodule* module,
                                       const void *image);

CUresult CUDAAPI CuModuleUnload      (CUmodule module);

CUresult CUDAAPI CuModuleGetFunction  (CUfunction* hfunc,
                                       CUmodule hmod,
                                       const char* name);

CUresult CUDAAPI CuTexObjectCreate    (CUtexObject *pTexObject,
                                       const CUDA_RESOURCE_DESC *pResDesc,
                                       const CUDA_TEXTURE_DESC *pTexDesc,
                                       const CUDA_RESOURCE_VIEW_DESC *pResViewDesc);

CUresult CUDAAPI CuTexObjectDestroy   (CUtexObject texObject);

CUresult CUDAAPI CuLaunchKernel       (CUfunction f,
                                       unsigned int gridDimX,
                                       unsigned int gridDimY,
                                       unsigned int gridDimZ,
                                       unsigned int blockDimX,
                                       unsigned int blockDimY,
                                       unsigned int blockDimZ,
                                       unsigned int sharedMemBytes,
                                       CUstream hStream,
                                       void **kernelParams,
                                       void **extra);

/* cudaGL.h */
CUresult CUDAAPI CuGraphicsGLRegisterImage  (CUgraphicsResource * pCudaResource,
                                             unsigned int image,
                                             unsigned int target,
                                             unsigned int Flags);

CUresult CUDAAPI CuGraphicsGLRegisterBuffer (CUgraphicsResource * pCudaResource,
                                             unsigned int buffer,
                                             unsigned int Flags);

CUresult CUDAAPI CuGraphicsResourceSetMapFlags (CUgraphicsResource resource,
                                                unsigned int flags);

CUresult CUDAAPI CuGLGetDevices (unsigned int * pCudaDeviceCount,
                                 CUdevice * pCudaDevices,
                                 unsigned int cudaDeviceCount,
                                 CUGLDeviceList deviceList);

G_END_DECLS
#endif /* __GST_CUDA_LOADER_H__ */
