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

G_GNUC_INTERNAL
gboolean gst_cuda_load_library (void);

/* cuda.h */
G_GNUC_INTERNAL
CUresult CUDAAPI CuInit             (unsigned int Flags);

G_GNUC_INTERNAL
CUresult CUDAAPI CuGetErrorName     (CUresult error,
                                     const char **pStr);

G_GNUC_INTERNAL
CUresult CUDAAPI CuGetErrorString   (CUresult error,
                                     const char **pStr);

G_GNUC_INTERNAL
CUresult CUDAAPI CuCtxCreate        (CUcontext * pctx,
                                     unsigned int flags,
                                     CUdevice dev);

G_GNUC_INTERNAL
CUresult CUDAAPI CuCtxDestroy       (CUcontext ctx);

G_GNUC_INTERNAL
CUresult CUDAAPI CuCtxPopCurrent    (CUcontext * pctx);

G_GNUC_INTERNAL
CUresult CUDAAPI CuCtxPushCurrent   (CUcontext ctx);

G_GNUC_INTERNAL
CUresult CUDAAPI CuGraphicsMapResources     (unsigned int count,
                                             CUgraphicsResource * resources,
                                             CUstream hStream);

G_GNUC_INTERNAL
CUresult CUDAAPI CuGraphicsUnmapResources   (unsigned int count,
                                             CUgraphicsResource * resources,
                                             CUstream hStream);

G_GNUC_INTERNAL
CUresult CUDAAPI CuGraphicsSubResourceGetMappedArray    (CUarray * pArray,
                                                         CUgraphicsResource resource,
                                                         unsigned int arrayIndex,
                                                         unsigned int mipLevel);

G_GNUC_INTERNAL
CUresult CUDAAPI CuGraphicsResourceGetMappedPointer     (CUdeviceptr * pDevPtr,
                                                         size_t * pSize,
                                                         CUgraphicsResource resource);

G_GNUC_INTERNAL
CUresult CUDAAPI CuGraphicsUnregisterResource           (CUgraphicsResource resource);

G_GNUC_INTERNAL
CUresult CUDAAPI CuMemAlloc         (CUdeviceptr * dptr,
                                     unsigned int bytesize);

G_GNUC_INTERNAL
CUresult CUDAAPI CuMemAllocPitch    (CUdeviceptr * dptr,
                                     size_t * pPitch,
                                     size_t WidthInBytes,
                                     size_t Height,
                                     unsigned int ElementSizeBytes);

G_GNUC_INTERNAL
CUresult CUDAAPI CuMemcpy2D         (const CUDA_MEMCPY2D * pCopy);

G_GNUC_INTERNAL
CUresult CUDAAPI CuMemcpy2DAsync    (const CUDA_MEMCPY2D *pCopy, CUstream hStream);

G_GNUC_INTERNAL
CUresult CUDAAPI CuMemFree          (CUdeviceptr dptr);

G_GNUC_INTERNAL
CUresult CUDAAPI CuStreamCreate     (CUstream *phStream,
                                     unsigned int Flags);

G_GNUC_INTERNAL
CUresult CUDAAPI CuStreamDestroy    (CUstream hStream);

G_GNUC_INTERNAL
CUresult CUDAAPI CuStreamSynchronize (CUstream hStream);

G_GNUC_INTERNAL
CUresult CUDAAPI CuDeviceGet        (CUdevice * device,
                                     int ordinal);

G_GNUC_INTERNAL
CUresult CUDAAPI CuDeviceGetCount   (int *count);

G_GNUC_INTERNAL
CUresult CUDAAPI CuDeviceGetName    (char *name,
                                     int len,
                                     CUdevice dev);

G_GNUC_INTERNAL
CUresult CUDAAPI CuDeviceGetAttribute (int *pi,
                                       CUdevice_attribute attrib,
                                       CUdevice dev);

/* cudaGL.h */
G_GNUC_INTERNAL
CUresult CUDAAPI CuGraphicsGLRegisterImage  (CUgraphicsResource * pCudaResource,
                                             unsigned int image,
                                             unsigned int target,
                                             unsigned int Flags);

G_GNUC_INTERNAL
CUresult CUDAAPI CuGraphicsGLRegisterBuffer (CUgraphicsResource * pCudaResource,
                                             unsigned int buffer,
                                             unsigned int Flags);

G_GNUC_INTERNAL
CUresult CUDAAPI CuGraphicsResourceSetMapFlags (CUgraphicsResource resource,
                                                unsigned int flags);

G_GNUC_INTERNAL
CUresult CUDAAPI CuGLGetDevices (unsigned int * pCudaDeviceCount,
                                 CUdevice * pCudaDevices,
                                 unsigned int cudaDeviceCount,
                                 CUGLDeviceList deviceList);

G_END_DECLS
#endif /* __GST_CUDA_LOADER_H__ */
