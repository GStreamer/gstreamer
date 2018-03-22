/*
 * Copyright 2018 Red Hat, Inc. and/or its affiliates.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <gst/gst.h>
#include <gst/gl/gstglfuncs.h>
#include <dynlink_cuda.h>
#include <gmodule.h>

/* Types that are missing from included headers */
typedef CUresult CUDAAPI tcuGetErrorName (CUresult error, const char **pStr);
typedef CUresult CUDAAPI tcuGetErrorString (CUresult error, const char **pStr);
typedef CUresult CUDAAPI tcuGraphicsGLRegisterImage (CUgraphicsResource *
    pCudaResource, GLuint image, GLenum target, unsigned int Flags);

tcuInit *_cuInit;
tcuDriverGetVersion *cuDriverGetVersion;
tcuCtxCreate *cuCtxCreate;
tcuCtxDestroy *cuCtxDestroy;
tcuCtxPopCurrent *cuCtxPopCurrent;
tcuMemcpy2D *cuMemcpy2D;
tcuGraphicsUnregisterResource *cuGraphicsUnregisterResource;
tcuGraphicsSubResourceGetMappedArray *cuGraphicsSubResourceGetMappedArray;
tcuGraphicsMapResources *cuGraphicsMapResources;
tcuGraphicsUnmapResources *cuGraphicsUnmapResources;

tcuGetErrorName *cuGetErrorName;
tcuGetErrorString *cuGetErrorString;
tcuGraphicsGLRegisterImage *cuGraphicsGLRegisterImage;

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
static char CudaLibName[] = "nvcuda.dll";
#elif defined(__unix__)
static char CudaLibName[] = "libcuda.so";
#endif

#define STRINGIFY(X) #X
#define GET_PROC(name)                                                     \
  if (!g_module_symbol (module, #name, (gpointer *)&name)) {               \
    GST_ERROR("%s", g_module_error());                                     \
    g_module_close(module);                                                \
    return CUDA_ERROR_UNKNOWN;                                             \
   }

#define GET_PROC_V2(name)                                                  \
  if(!g_module_symbol (module, STRINGIFY(name##_v2), (gpointer *)&name)) { \
    GST_ERROR("%s", g_module_error());                                     \
    g_module_close(module);                                                \
    return CUDA_ERROR_UNKNOWN;                                             \
  }

CUresult CUDAAPI
cuInit (unsigned int Flags, int cudaVersion, void *pHandleDriver)
{
  GModule *module;
  int driverVer = 1000;

  module = g_module_open (CudaLibName, G_MODULE_BIND_LAZY);
  if (module == NULL) {
    GST_ERROR ("%s", g_module_error ());
    return CUDA_ERROR_UNKNOWN;
  }
  //Init
  g_module_symbol (module, "cuInit", (gpointer *) & _cuInit);
  if (_cuInit == NULL || _cuInit (Flags) != CUDA_SUCCESS) {
    GST_ERROR ("Failed to init cuda\n");
    return CUDA_ERROR_UNKNOWN;
  }

  GET_PROC (cuDriverGetVersion);
  if (cuDriverGetVersion == NULL
      || cuDriverGetVersion (&driverVer) != CUDA_SUCCESS) {
    GST_ERROR ("Failed to get cuda version\n");
    return CUDA_ERROR_UNKNOWN;
  }

  if (cudaVersion < 4000 || __CUDA_API_VERSION < 4000) {
    GST_ERROR ("cuda version or cuda api version is too old\n");
    return CUDA_ERROR_UNKNOWN;
  }

  GET_PROC (cuCtxDestroy);
  GET_PROC (cuCtxPopCurrent);
  GET_PROC (cuGetErrorName);
  GET_PROC (cuGetErrorString);

  GET_PROC_V2 (cuCtxDestroy);
  GET_PROC_V2 (cuCtxPopCurrent);
  GET_PROC_V2 (cuCtxCreate);
  GET_PROC_V2 (cuMemcpy2D);
  GET_PROC (cuGraphicsGLRegisterImage);
  GET_PROC (cuGraphicsGLRegisterImage);
  GET_PROC (cuGraphicsUnregisterResource);
  GET_PROC (cuGraphicsSubResourceGetMappedArray);
  GET_PROC (cuGraphicsMapResources);
  GET_PROC (cuGraphicsUnmapResources);

  return CUDA_SUCCESS;
}
