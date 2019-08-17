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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstcudaloader.h"
#include <gmodule.h>

#ifndef G_OS_WIN32
#define CUDA_LIBNAME "libcuda.so.1"
#else
#define CUDA_LIBNAME "nvcuda.dll"
#endif

#define LOAD_SYMBOL(name,func) G_STMT_START { \
  if (!g_module_symbol (module, G_STRINGIFY (name), (gpointer *) &vtable->func)) { \
    GST_ERROR ("Failed to load '%s' from %s, %s", G_STRINGIFY (name), filename, g_module_error()); \
    goto error; \
  } \
} G_STMT_END;

typedef struct _GstNvCodecCudaVTable
{
  gboolean loaded;

    CUresult (*CuInit) (unsigned int Flags);
    CUresult (*CuGetErrorName) (CUresult error, const char **pStr);
    CUresult (*CuGetErrorString) (CUresult error, const char **pStr);

    CUresult (*CuCtxCreate) (CUcontext * pctx, unsigned int flags,
      CUdevice dev);
    CUresult (*CuCtxDestroy) (CUcontext ctx);
    CUresult (*CuCtxPopCurrent) (CUcontext * pctx);
    CUresult (*CuCtxPushCurrent) (CUcontext ctx);

    CUresult (*CuGraphicsMapResources) (unsigned int count,
      CUgraphicsResource * resources, CUstream hStream);
    CUresult (*CuGraphicsUnmapResources) (unsigned int count,
      CUgraphicsResource * resources, CUstream hStream);
    CUresult (*CuGraphicsSubResourceGetMappedArray) (CUarray * pArray,
      CUgraphicsResource resource, unsigned int arrayIndex,
      unsigned int mipLevel);
    CUresult (*CuGraphicsResourceGetMappedPointer) (CUdeviceptr * pDevPtr,
      size_t * pSize, CUgraphicsResource resource);
    CUresult (*CuGraphicsUnregisterResource) (CUgraphicsResource resource);

    CUresult (*CuMemAlloc) (CUdeviceptr * dptr, unsigned int bytesize);
    CUresult (*CuMemAllocPitch) (CUdeviceptr * dptr, size_t * pPitch,
      size_t WidthInBytes, size_t Height, unsigned int ElementSizeBytes);
    CUresult (*CuMemcpy2D) (const CUDA_MEMCPY2D * pCopy);
    CUresult (*CuMemcpy2DAsync) (const CUDA_MEMCPY2D * pCopy, CUstream hStream);
    CUresult (*CuMemFree) (CUdeviceptr dptr);
    CUresult (*CuStreamCreate) (CUstream * phStream, unsigned int Flags);
    CUresult (*CuStreamDestroy) (CUstream hStream);
    CUresult (*CuStreamSynchronize) (CUstream hStream);

    CUresult (*CuDeviceGet) (CUdevice * device, int ordinal);
    CUresult (*CuDeviceGetCount) (int *count);
    CUresult (*CuDeviceGetName) (char *name, int len, CUdevice dev);
    CUresult (*CuDeviceGetAttribute) (int *pi, CUdevice_attribute attrib,
      CUdevice dev);

    CUresult (*CuGraphicsGLRegisterImage) (CUgraphicsResource * pCudaResource,
      unsigned int image, unsigned int target, unsigned int Flags);
    CUresult (*CuGraphicsGLRegisterBuffer) (CUgraphicsResource * pCudaResource,
      unsigned int buffer, unsigned int Flags);
    CUresult (*CuGraphicsResourceSetMapFlags) (CUgraphicsResource resource,
      unsigned int flags);
} GstNvCodecCudaVTable;

static GstNvCodecCudaVTable gst_cuda_vtable = { 0, };

gboolean
gst_cuda_load_library (void)
{
  GModule *module;
  const gchar *filename = CUDA_LIBNAME;
  GstNvCodecCudaVTable *vtable;

  if (gst_cuda_vtable.loaded)
    return TRUE;

  module = g_module_open (filename, G_MODULE_BIND_LAZY);
  if (module == NULL) {
    GST_WARNING ("Could not open library %s, %s", filename, g_module_error ());
    return FALSE;
  }

  vtable = &gst_cuda_vtable;

  /* cuda.h */
  LOAD_SYMBOL (cuInit, CuInit);
  LOAD_SYMBOL (cuGetErrorName, CuGetErrorName);
  LOAD_SYMBOL (cuGetErrorString, CuGetErrorString);
  LOAD_SYMBOL (cuCtxCreate, CuCtxCreate);
  LOAD_SYMBOL (cuCtxDestroy, CuCtxDestroy);
  LOAD_SYMBOL (cuCtxPopCurrent, CuCtxPopCurrent);
  LOAD_SYMBOL (cuCtxPushCurrent, CuCtxPushCurrent);

  LOAD_SYMBOL (cuGraphicsMapResources, CuGraphicsMapResources);
  LOAD_SYMBOL (cuGraphicsUnmapResources, CuGraphicsUnmapResources);
  LOAD_SYMBOL (cuGraphicsSubResourceGetMappedArray,
      CuGraphicsSubResourceGetMappedArray);
  LOAD_SYMBOL (cuGraphicsResourceGetMappedPointer,
      CuGraphicsResourceGetMappedPointer);
  LOAD_SYMBOL (cuGraphicsUnregisterResource, CuGraphicsUnregisterResource);

  LOAD_SYMBOL (cuMemAlloc, CuMemAlloc);
  LOAD_SYMBOL (cuMemAllocPitch, CuMemAllocPitch);
  LOAD_SYMBOL (cuMemcpy2D, CuMemcpy2D);
  LOAD_SYMBOL (cuMemcpy2DAsync, CuMemcpy2DAsync);
  LOAD_SYMBOL (cuMemFree, CuMemFree);

  LOAD_SYMBOL (cuStreamCreate, CuStreamCreate);
  LOAD_SYMBOL (cuStreamDestroy, CuStreamDestroy);
  LOAD_SYMBOL (cuStreamSynchronize, CuStreamSynchronize);

  LOAD_SYMBOL (cuDeviceGet, CuDeviceGet);
  LOAD_SYMBOL (cuDeviceGetCount, CuDeviceGetCount);
  LOAD_SYMBOL (cuDeviceGetName, CuDeviceGetName);
  LOAD_SYMBOL (cuDeviceGetAttribute, CuDeviceGetAttribute);

  /* cudaGL.h */
  LOAD_SYMBOL (cuGraphicsGLRegisterImage, CuGraphicsGLRegisterImage);
  LOAD_SYMBOL (cuGraphicsGLRegisterBuffer, CuGraphicsGLRegisterBuffer);
  LOAD_SYMBOL (cuGraphicsResourceSetMapFlags, CuGraphicsResourceSetMapFlags);

  vtable->loaded = TRUE;

  return TRUE;

error:
  g_module_close (module);

  return FALSE;
}

CUresult
CuInit (unsigned int Flags)
{
  g_assert (gst_cuda_vtable.CuInit != NULL);

  return gst_cuda_vtable.CuInit (Flags);
}

CUresult
CuGetErrorName (CUresult error, const char **pStr)
{
  g_assert (gst_cuda_vtable.CuGetErrorName != NULL);

  return gst_cuda_vtable.CuGetErrorName (error, pStr);
}

CUresult
CuGetErrorString (CUresult error, const char **pStr)
{
  g_assert (gst_cuda_vtable.CuGetErrorString != NULL);

  return gst_cuda_vtable.CuGetErrorString (error, pStr);
}

CUresult
CuCtxCreate (CUcontext * pctx, unsigned int flags, CUdevice dev)
{
  g_assert (gst_cuda_vtable.CuCtxCreate != NULL);

  return gst_cuda_vtable.CuCtxCreate (pctx, flags, dev);
}

CUresult
CuCtxDestroy (CUcontext ctx)
{
  g_assert (gst_cuda_vtable.CuCtxDestroy != NULL);

  return gst_cuda_vtable.CuCtxDestroy (ctx);
}

CUresult
CuCtxPopCurrent (CUcontext * pctx)
{
  g_assert (gst_cuda_vtable.CuCtxPopCurrent != NULL);

  return gst_cuda_vtable.CuCtxPopCurrent (pctx);
}

CUresult
CuCtxPushCurrent (CUcontext ctx)
{
  g_assert (gst_cuda_vtable.CuCtxPushCurrent != NULL);

  return gst_cuda_vtable.CuCtxPushCurrent (ctx);
}

CUresult
CuGraphicsMapResources (unsigned int count, CUgraphicsResource * resources,
    CUstream hStream)
{
  g_assert (gst_cuda_vtable.CuGraphicsMapResources != NULL);

  return gst_cuda_vtable.CuGraphicsMapResources (count, resources, hStream);
}

CUresult
CuGraphicsUnmapResources (unsigned int count, CUgraphicsResource * resources,
    CUstream hStream)
{
  g_assert (gst_cuda_vtable.CuGraphicsUnmapResources != NULL);

  return gst_cuda_vtable.CuGraphicsUnmapResources (count, resources, hStream);
}

CUresult
CuGraphicsSubResourceGetMappedArray (CUarray * pArray,
    CUgraphicsResource resource, unsigned int arrayIndex, unsigned int mipLevel)
{
  g_assert (gst_cuda_vtable.CuGraphicsSubResourceGetMappedArray != NULL);

  return gst_cuda_vtable.CuGraphicsSubResourceGetMappedArray (pArray, resource,
      arrayIndex, mipLevel);
}

CUresult
CuGraphicsResourceGetMappedPointer (CUdeviceptr * pDevPtr, size_t * pSize,
    CUgraphicsResource resource)
{
  g_assert (gst_cuda_vtable.CuGraphicsResourceGetMappedPointer != NULL);

  return gst_cuda_vtable.CuGraphicsResourceGetMappedPointer (pDevPtr, pSize,
      resource);
}

CUresult
CuGraphicsUnregisterResource (CUgraphicsResource resource)
{
  g_assert (gst_cuda_vtable.CuGraphicsUnregisterResource != NULL);

  return gst_cuda_vtable.CuGraphicsUnregisterResource (resource);
}

CUresult
CuMemAlloc (CUdeviceptr * dptr, unsigned int bytesize)
{
  g_assert (gst_cuda_vtable.CuMemAlloc != NULL);

  return gst_cuda_vtable.CuMemAlloc (dptr, bytesize);
}

CUresult
CuMemAllocPitch (CUdeviceptr * dptr, size_t * pPitch, size_t WidthInBytes,
    size_t Height, unsigned int ElementSizeBytes)
{
  g_assert (gst_cuda_vtable.CuMemAllocPitch != NULL);

  return gst_cuda_vtable.CuMemAllocPitch (dptr, pPitch, WidthInBytes, Height,
      ElementSizeBytes);
}

CUresult
CuMemcpy2D (const CUDA_MEMCPY2D * pCopy)
{
  g_assert (gst_cuda_vtable.CuMemcpy2D != NULL);

  return gst_cuda_vtable.CuMemcpy2D (pCopy);
}

CUresult
CuMemcpy2DAsync (const CUDA_MEMCPY2D * pCopy, CUstream hStream)
{
  g_assert (gst_cuda_vtable.CuMemcpy2DAsync != NULL);

  return gst_cuda_vtable.CuMemcpy2DAsync (pCopy, hStream);
}

CUresult
CuMemFree (CUdeviceptr dptr)
{
  g_assert (gst_cuda_vtable.CuMemFree != NULL);

  return gst_cuda_vtable.CuMemFree (dptr);
}

CUresult
CuStreamCreate (CUstream * phStream, unsigned int Flags)
{
  g_assert (gst_cuda_vtable.CuStreamCreate != NULL);

  return gst_cuda_vtable.CuStreamCreate (phStream, Flags);
}

CUresult
CuStreamDestroy (CUstream hStream)
{
  g_assert (gst_cuda_vtable.CuStreamDestroy != NULL);

  return gst_cuda_vtable.CuStreamDestroy (hStream);
}

CUresult
CuStreamSynchronize (CUstream hStream)
{
  g_assert (gst_cuda_vtable.CuStreamSynchronize != NULL);

  return gst_cuda_vtable.CuStreamSynchronize (hStream);
}

CUresult
CuDeviceGet (CUdevice * device, int ordinal)
{
  g_assert (gst_cuda_vtable.CuDeviceGet != NULL);

  return gst_cuda_vtable.CuDeviceGet (device, ordinal);
}

CUresult
CuDeviceGetCount (int *count)
{
  g_assert (gst_cuda_vtable.CuDeviceGetCount != NULL);

  return gst_cuda_vtable.CuDeviceGetCount (count);
}

CUresult
CuDeviceGetName (char *name, int len, CUdevice dev)
{
  g_assert (gst_cuda_vtable.CuDeviceGetName != NULL);

  return gst_cuda_vtable.CuDeviceGetName (name, len, dev);
}

CUresult
CuDeviceGetAttribute (int *pi, CUdevice_attribute attrib, CUdevice dev)
{
  g_assert (gst_cuda_vtable.CuDeviceGetAttribute != NULL);

  return gst_cuda_vtable.CuDeviceGetAttribute (pi, attrib, dev);
}

/* cudaGL.h */
CUresult
CuGraphicsGLRegisterImage (CUgraphicsResource * pCudaResource,
    unsigned int image, unsigned int target, unsigned int Flags)
{
  g_assert (gst_cuda_vtable.CuGraphicsGLRegisterImage != NULL);

  return gst_cuda_vtable.CuGraphicsGLRegisterImage (pCudaResource, image,
      target, Flags);
}

CUresult
CuGraphicsGLRegisterBuffer (CUgraphicsResource * pCudaResource,
    unsigned int buffer, unsigned int Flags)
{
  g_assert (gst_cuda_vtable.CuGraphicsGLRegisterBuffer != NULL);

  return gst_cuda_vtable.CuGraphicsGLRegisterBuffer (pCudaResource, buffer,
      Flags);
}

CUresult
CuGraphicsResourceSetMapFlags (CUgraphicsResource resource, unsigned int flags)
{
  g_assert (gst_cuda_vtable.CuGraphicsResourceSetMapFlags != NULL);

  return gst_cuda_vtable.CuGraphicsResourceSetMapFlags (resource, flags);
}
