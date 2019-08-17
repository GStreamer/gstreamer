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

#include <gst/gst.h>
#include <cuda.h>

G_BEGIN_DECLS

G_GNUC_INTERNAL
gboolean gst_cuda_load_library (void);

/* cuda.h */
G_GNUC_INTERNAL
CUresult CuInit             (unsigned int Flags);

G_GNUC_INTERNAL
CUresult CuGetErrorName     (CUresult error,
                             const char **pStr);

G_GNUC_INTERNAL
CUresult CuGetErrorString   (CUresult error,
                             const char **pStr);

G_GNUC_INTERNAL
CUresult CuCtxCreate        (CUcontext * pctx,
                             unsigned int flags,
                             CUdevice dev);

G_GNUC_INTERNAL
CUresult CuCtxDestroy       (CUcontext ctx);

G_GNUC_INTERNAL
CUresult CuCtxPopCurrent    (CUcontext * pctx);

G_GNUC_INTERNAL
CUresult CuCtxPushCurrent   (CUcontext ctx);

G_GNUC_INTERNAL
CUresult CuGraphicsMapResources     (unsigned int count,
                                     CUgraphicsResource * resources,
                                     CUstream hStream);

G_GNUC_INTERNAL
CUresult CuGraphicsUnmapResources   (unsigned int count,
                                     CUgraphicsResource * resources,
                                     CUstream hStream);

G_GNUC_INTERNAL
CUresult CuGraphicsSubResourceGetMappedArray    (CUarray * pArray,
                                                 CUgraphicsResource resource,
                                                 unsigned int arrayIndex,
                                                 unsigned int mipLevel);

G_GNUC_INTERNAL
CUresult CuGraphicsResourceGetMappedPointer     (CUdeviceptr * pDevPtr,
                                                 size_t * pSize,
                                                 CUgraphicsResource resource);

G_GNUC_INTERNAL
CUresult CuGraphicsUnregisterResource           (CUgraphicsResource resource);

G_GNUC_INTERNAL
CUresult CuMemAlloc         (CUdeviceptr * dptr,
                             unsigned int bytesize);

G_GNUC_INTERNAL
CUresult CuMemAllocPitch    (CUdeviceptr * dptr,
                             size_t * pPitch,
                             size_t WidthInBytes,
                             size_t Height,
                             unsigned int ElementSizeBytes);

G_GNUC_INTERNAL
CUresult CuMemcpy2D         (const CUDA_MEMCPY2D * pCopy);

G_GNUC_INTERNAL
CUresult CuMemcpy2DAsync    (const CUDA_MEMCPY2D *pCopy, CUstream hStream);

G_GNUC_INTERNAL
CUresult CuMemFree          (CUdeviceptr dptr);

G_GNUC_INTERNAL
CUresult CuStreamCreate     (CUstream *phStream,
                             unsigned int Flags);

G_GNUC_INTERNAL
CUresult CuStreamDestroy    (CUstream hStream);

G_GNUC_INTERNAL
CUresult CuStreamSynchronize (CUstream hStream);

G_GNUC_INTERNAL
CUresult CuDeviceGet        (CUdevice * device,
                             int ordinal);

G_GNUC_INTERNAL
CUresult CuDeviceGetCount   (int *count);

G_GNUC_INTERNAL
CUresult CuDeviceGetName    (char *name,
                             int len,
                             CUdevice dev);

G_GNUC_INTERNAL
CUresult CuDeviceGetAttribute (int *pi,
                               CUdevice_attribute attrib,
                               CUdevice dev);

/* cudaGL.h */
G_GNUC_INTERNAL
CUresult CuGraphicsGLRegisterImage  (CUgraphicsResource * pCudaResource,
                                     unsigned int image,
                                     unsigned int target,
                                     unsigned int Flags);

G_GNUC_INTERNAL
CUresult CuGraphicsGLRegisterBuffer (CUgraphicsResource * pCudaResource,
                                     unsigned int buffer,
                                     unsigned int Flags);

G_GNUC_INTERNAL
CUresult CuGraphicsResourceSetMapFlags (CUgraphicsResource resource,
                                        unsigned int flags);

G_END_DECLS
#endif /* __GST_CUDA_LOADER_H__ */
