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
#include "dynlink_nvcuvid.h"
#include <gmodule.h>

tcuvidCreateVideoParser *cuvidCreateVideoParser;
tcuvidParseVideoData *cuvidParseVideoData;
tcuvidDestroyVideoParser *cuvidDestroyVideoParser;
tcuvidCreateDecoder *cuvidCreateDecoder;
tcuvidDestroyDecoder *cuvidDestroyDecoder;
tcuvidDecodePicture *cuvidDecodePicture;
tcuvidMapVideoFrame *cuvidMapVideoFrame;
tcuvidUnmapVideoFrame *cuvidUnmapVideoFrame;
#if defined(__x86_64) || defined(AMD64) || defined(_M_AMD64)
tcuvidMapVideoFrame64 *cuvidMapVideoFrame64;
tcuvidUnmapVideoFrame64 *cuvidUnmapVideoFrame64;
#endif
tcuvidCtxLockCreate *cuvidCtxLockCreate;
tcuvidCtxLockDestroy *cuvidCtxLockDestroy;
tcuvidCtxLock *cuvidCtxLock;
tcuvidCtxUnlock *cuvidCtxUnlock;

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
static char CudaLibName[] = "nvcuvid.dll";
#elif defined(__unix__)
static char CudaLibName[] = "libnvcuvid.so";
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
cuvidInit (unsigned int Flags)
{
  GModule *module;

  module = g_module_open (CudaLibName, G_MODULE_BIND_LAZY);
  if (module == NULL) {
    GST_ERROR ("%s", g_module_error ());
    return CUDA_ERROR_UNKNOWN;
  }

  GET_PROC (cuvidCreateVideoParser);
  GET_PROC (cuvidParseVideoData);
  GET_PROC (cuvidDestroyVideoParser);

  GET_PROC (cuvidCreateDecoder);
  GET_PROC (cuvidDestroyDecoder);
  GET_PROC (cuvidDecodePicture);

#if defined(__x86_64) || defined(AMD64) || defined(_M_AMD64)
  GET_PROC (cuvidMapVideoFrame64);
  GET_PROC (cuvidUnmapVideoFrame64);
  cuvidMapVideoFrame = cuvidMapVideoFrame64;
  cuvidUnmapVideoFrame = cuvidUnmapVideoFrame64;
#else
  GET_PROC (cuvidMapVideoFrame);
  GET_PROC (cuvidUnmapVideoFrame);
#endif

  GET_PROC (cuvidCtxLockCreate);
  GET_PROC (cuvidCtxLockDestroy);
  GET_PROC (cuvidCtxLock);
  GET_PROC (cuvidCtxUnlock);

  return CUDA_SUCCESS;
}
