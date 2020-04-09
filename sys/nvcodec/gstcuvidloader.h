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

#ifndef __GST_CUVID_LOADER_H__
#define __GST_CUVID_LOADER_H__

#include <gst/gst.h>
#include "stub/cuda.h"
#include "nvcuvid.h"

G_BEGIN_DECLS

/* cuvid.h */
G_GNUC_INTERNAL
gboolean gst_cuvid_load_library     (guint api_major_ver,
                                     guint api_minor_ver);

G_GNUC_INTERNAL
gboolean gst_cuvid_get_api_version  (guint * api_major_ver,
                                     guint * api_minor_ver);

G_GNUC_INTERNAL
gboolean gst_cuvid_can_get_decoder_caps (void);

G_GNUC_INTERNAL
CUresult CUDAAPI CuvidCtxLockCreate         (CUvideoctxlock * pLock,
                                             CUcontext ctx);

G_GNUC_INTERNAL
CUresult CUDAAPI CuvidCtxLockDestroy        (CUvideoctxlock lck);

G_GNUC_INTERNAL
CUresult CUDAAPI CuvidCtxLock               (CUvideoctxlock lck,
                                             unsigned int reserved_flags);

G_GNUC_INTERNAL
CUresult CUDAAPI CuvidCtxUnlock             (CUvideoctxlock lck,
                                             unsigned int reserved_flags);

G_GNUC_INTERNAL
CUresult CUDAAPI CuvidCreateDecoder         (CUvideodecoder * phDecoder,
                                             CUVIDDECODECREATEINFO * pdci);

G_GNUC_INTERNAL
CUresult CUDAAPI CuvidDestroyDecoder        (CUvideodecoder hDecoder);

G_GNUC_INTERNAL
CUresult CUDAAPI CuvidDecodePicture         (CUvideodecoder hDecoder,
                                             CUVIDPICPARAMS * pPicParams);

G_GNUC_INTERNAL
CUresult CUDAAPI CuvidCreateVideoParser     (CUvideoparser * pObj,
                                             CUVIDPARSERPARAMS * pParams);

G_GNUC_INTERNAL
CUresult CUDAAPI CuvidParseVideoData        (CUvideoparser obj,
                                             CUVIDSOURCEDATAPACKET * pPacket);

G_GNUC_INTERNAL
CUresult CUDAAPI CuvidDestroyVideoParser    (CUvideoparser obj);

G_GNUC_INTERNAL
CUresult CUDAAPI CuvidMapVideoFrame         (CUvideodecoder hDecoder,
                                             int nPicIdx,
                                             guintptr * pDevPtr,
                                             unsigned int *pPitch,
                                             CUVIDPROCPARAMS * pVPP);

G_GNUC_INTERNAL
CUresult CUDAAPI CuvidUnmapVideoFrame       (CUvideodecoder hDecoder,
                                             guintptr DevPtr);
G_GNUC_INTERNAL
CUresult CUDAAPI CuvidGetDecoderCaps        (CUVIDDECODECAPS * pdc);

G_END_DECLS
#endif /* __GST_CUVID_LOADER_H__ */
