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

#include "gstcuvidloader.h"
#include <gmodule.h>

#ifdef G_OS_WIN32
#define NVCUVID_LIBNAME "nvcuvid.dll"
#else
#define NVCUVID_LIBNAME "libnvcuvid.so.1"
#endif

#define LOAD_SYMBOL(name,func,mandatory) G_STMT_START { \
  if (!g_module_symbol (module, G_STRINGIFY (name), (gpointer *) &vtable->func)) { \
    if (mandatory) { \
      GST_ERROR ("Failed to load '%s' from %s, %s", G_STRINGIFY (name), filename, g_module_error()); \
      goto error; \
    } \
    GST_WARNING ("Failed to load '%s' from %s, %s", G_STRINGIFY (name), filename, g_module_error()); \
  } \
} G_STMT_END;

typedef struct _GstnvdecCuvidVTable
{
  gboolean loaded;

    CUresult (*CuvidCtxLockCreate) (CUvideoctxlock * pLock, CUcontext ctx);
    CUresult (*CuvidCtxLockDestroy) (CUvideoctxlock lck);
    CUresult (*CuvidCtxLock) (CUvideoctxlock lck, unsigned int reserved_flags);
    CUresult (*CuvidCtxUnlock) (CUvideoctxlock lck,
      unsigned int reserved_flags);
    CUresult (*CuvidCreateDecoder) (CUvideodecoder * phDecoder,
      CUVIDDECODECREATEINFO * pdci);
    CUresult (*CuvidDestroyDecoder) (CUvideodecoder hDecoder);
    CUresult (*CuvidDecodePicture) (CUvideodecoder hDecoder,
      CUVIDPICPARAMS * pPicParams);
    CUresult (*CuvidCreateVideoParser) (CUvideoparser * pObj,
      CUVIDPARSERPARAMS * pParams);
    CUresult (*CuvidParseVideoData) (CUvideoparser obj,
      CUVIDSOURCEDATAPACKET * pPacket);
    CUresult (*CuvidDestroyVideoParser) (CUvideoparser obj);
    CUresult (*CuvidMapVideoFrame) (CUvideodecoder hDecoder, int nPicIdx,
      guintptr * pDevPtr, unsigned int *pPitch, CUVIDPROCPARAMS * pVPP);
    CUresult (*CuvidUnmapVideoFrame) (CUvideodecoder hDecoder, guintptr DevPtr);
    CUresult (*CuvidGetDecoderCaps) (CUVIDDECODECAPS * pdc);
} GstnvdecCuvidVTable;

static GstnvdecCuvidVTable gst_cuvid_vtable = { 0, };

gboolean
gst_cuvid_load_library (void)
{
  GModule *module;
  const gchar *filename = NVCUVID_LIBNAME;
  GstnvdecCuvidVTable *vtable;

  if (gst_cuvid_vtable.loaded)
    return TRUE;

  module = g_module_open (filename, G_MODULE_BIND_LAZY);
  if (module == NULL) {
    GST_WARNING ("Could not open library %s, %s", filename, g_module_error ());
    return FALSE;
  }

  vtable = &gst_cuvid_vtable;

  LOAD_SYMBOL (cuvidCtxLockCreate, CuvidCtxLockCreate, TRUE);
  LOAD_SYMBOL (cuvidCtxLockDestroy, CuvidCtxLockDestroy, TRUE);
  LOAD_SYMBOL (cuvidCtxLock, CuvidCtxLock, TRUE);
  LOAD_SYMBOL (cuvidCtxUnlock, CuvidCtxUnlock, TRUE);
  LOAD_SYMBOL (cuvidCreateDecoder, CuvidCreateDecoder, TRUE);
  LOAD_SYMBOL (cuvidDestroyDecoder, CuvidDestroyDecoder, TRUE);
  LOAD_SYMBOL (cuvidDecodePicture, CuvidDecodePicture, TRUE);
  LOAD_SYMBOL (cuvidCreateVideoParser, CuvidCreateVideoParser, TRUE);
  LOAD_SYMBOL (cuvidParseVideoData, CuvidParseVideoData, TRUE);
  LOAD_SYMBOL (cuvidDestroyVideoParser, CuvidDestroyVideoParser, TRUE);
  LOAD_SYMBOL (cuvidMapVideoFrame, CuvidMapVideoFrame, TRUE);
  LOAD_SYMBOL (cuvidUnmapVideoFrame, CuvidUnmapVideoFrame, TRUE);
  LOAD_SYMBOL (cuvidGetDecoderCaps, CuvidGetDecoderCaps, FALSE);

  vtable->loaded = TRUE;

  return TRUE;

error:
  g_module_close (module);

  return FALSE;
}

gboolean
gst_cuvid_can_get_decoder_caps (void)
{
  return ! !gst_cuvid_vtable.CuvidGetDecoderCaps;
}

CUresult
CuvidCtxLockCreate (CUvideoctxlock * pLock, CUcontext ctx)
{
  g_assert (gst_cuvid_vtable.CuvidCtxLockCreate != NULL);

  return gst_cuvid_vtable.CuvidCtxLockCreate (pLock, ctx);
}

CUresult
CuvidCtxLockDestroy (CUvideoctxlock lck)
{
  g_assert (gst_cuvid_vtable.CuvidCtxLockDestroy != NULL);

  return gst_cuvid_vtable.CuvidCtxLockDestroy (lck);
}

CUresult
CuvidCtxLock (CUvideoctxlock lck, unsigned int reserved_flags)
{
  g_assert (gst_cuvid_vtable.CuvidCtxLock != NULL);

  return gst_cuvid_vtable.CuvidCtxLock (lck, reserved_flags);
}

CUresult
CuvidCtxUnlock (CUvideoctxlock lck, unsigned int reserved_flags)
{
  g_assert (gst_cuvid_vtable.CuvidCtxLockDestroy != NULL);

  return gst_cuvid_vtable.CuvidCtxUnlock (lck, reserved_flags);
}

CUresult
CuvidCreateDecoder (CUvideodecoder * phDecoder, CUVIDDECODECREATEINFO * pdci)
{
  g_assert (gst_cuvid_vtable.CuvidCreateDecoder != NULL);

  return gst_cuvid_vtable.CuvidCreateDecoder (phDecoder, pdci);
}

CUresult
CuvidDestroyDecoder (CUvideodecoder hDecoder)
{
  g_assert (gst_cuvid_vtable.CuvidDestroyDecoder != NULL);

  return gst_cuvid_vtable.CuvidDestroyDecoder (hDecoder);
}

CUresult
CuvidDecodePicture (CUvideodecoder hDecoder, CUVIDPICPARAMS * pPicParams)
{
  g_assert (gst_cuvid_vtable.CuvidDecodePicture != NULL);

  return gst_cuvid_vtable.CuvidDecodePicture (hDecoder, pPicParams);
}

CUresult
CuvidCreateVideoParser (CUvideoparser * pObj, CUVIDPARSERPARAMS * pParams)
{
  g_assert (gst_cuvid_vtable.CuvidCreateVideoParser != NULL);

  return gst_cuvid_vtable.CuvidCreateVideoParser (pObj, pParams);
}

CUresult
CuvidParseVideoData (CUvideoparser obj, CUVIDSOURCEDATAPACKET * pPacket)
{
  g_assert (gst_cuvid_vtable.CuvidParseVideoData != NULL);

  return gst_cuvid_vtable.CuvidParseVideoData (obj, pPacket);
}

CUresult
CuvidDestroyVideoParser (CUvideoparser obj)
{
  g_assert (gst_cuvid_vtable.CuvidDestroyVideoParser != NULL);

  return gst_cuvid_vtable.CuvidDestroyVideoParser (obj);
}

CUresult
CuvidMapVideoFrame (CUvideodecoder hDecoder, int nPicIdx,
    guintptr * pDevPtr, unsigned int *pPitch, CUVIDPROCPARAMS * pVPP)
{
  g_assert (gst_cuvid_vtable.CuvidMapVideoFrame != NULL);

  return gst_cuvid_vtable.CuvidMapVideoFrame (hDecoder, nPicIdx, pDevPtr,
      pPitch, pVPP);
}

CUresult
CuvidUnmapVideoFrame (CUvideodecoder hDecoder, guintptr DevPtr)
{
  g_assert (gst_cuvid_vtable.CuvidUnmapVideoFrame != NULL);

  return gst_cuvid_vtable.CuvidUnmapVideoFrame (hDecoder, DevPtr);
}

CUresult
CuvidGetDecoderCaps (CUVIDDECODECAPS * pdc)
{
  g_assert (gst_cuvid_vtable.CuvidGetDecoderCaps != NULL);

  return gst_cuvid_vtable.CuvidGetDecoderCaps (pdc);
}
