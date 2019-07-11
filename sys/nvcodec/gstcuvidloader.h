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
#include "nvcuvid.h"

G_BEGIN_DECLS

/* cuvid.h */
G_GNUC_INTERNAL
gboolean gst_cuvid_load_library     (void);

G_GNUC_INTERNAL
gboolean gst_cuvid_can_get_decoder_caps (void);

G_GNUC_INTERNAL
CUresult CuvidCtxLockCreate         (CUvideoctxlock * pLock, CUcontext ctx);

G_GNUC_INTERNAL
CUresult CuvidCtxLockDestroy        (CUvideoctxlock lck);

G_GNUC_INTERNAL
CUresult CuvidCtxLock               (CUvideoctxlock lck,
                                     unsigned int reserved_flags);

G_GNUC_INTERNAL
CUresult CuvidCtxUnlock             (CUvideoctxlock lck,
                                     unsigned int reserved_flags);

G_GNUC_INTERNAL
CUresult CuvidCreateDecoder         (CUvideodecoder * phDecoder,
                                     CUVIDDECODECREATEINFO * pdci);

G_GNUC_INTERNAL
CUresult CuvidDestroyDecoder        (CUvideodecoder hDecoder);

G_GNUC_INTERNAL
CUresult CuvidDecodePicture         (CUvideodecoder hDecoder,
                                     CUVIDPICPARAMS * pPicParams);

G_GNUC_INTERNAL
CUresult CuvidCreateVideoParser     (CUvideoparser * pObj,
                                     CUVIDPARSERPARAMS * pParams);

G_GNUC_INTERNAL
CUresult CuvidParseVideoData        (CUvideoparser obj,
                                     CUVIDSOURCEDATAPACKET * pPacket);

G_GNUC_INTERNAL
CUresult CuvidDestroyVideoParser    (CUvideoparser obj);

G_GNUC_INTERNAL
CUresult CuvidMapVideoFrame         (CUvideodecoder hDecoder,
                                     int nPicIdx,
                                     guintptr * pDevPtr,
                                     unsigned int *pPitch,
                                     CUVIDPROCPARAMS * pVPP);

G_GNUC_INTERNAL
CUresult CuvidUnmapVideoFrame       (CUvideodecoder hDecoder,
                                     guintptr DevPtr);
G_GNUC_INTERNAL
CUresult CuvidGetDecoderCaps        (CUVIDDECODECAPS * pdc);

G_END_DECLS
#endif /* __GST_CUVID_LOADER_H__ */
