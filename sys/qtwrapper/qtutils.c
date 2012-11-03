/*
 * GStreamer QuickTime codec mapping
 * Copyright <2006, 2007> Fluendo <gstreamer@fluendo.com>
 * Copyright <2006, 2007> Pioneers of the Inevitable <songbird@songbirdnest.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

#include <string.h>
#include <glib.h>

#include "qtutils.h"

gboolean
get_name_info_from_component (Component componentID,
    ComponentDescription * desc, gchar ** name, gchar ** info)
{
  Handle nameHandle = NewHandle (200);
  Handle infoHandle = NewHandle (200);
  gchar *tmpname;
  gchar *tmpinfo;
  OSErr result;
  gboolean ret = TRUE;

  result = GetComponentInfo (componentID, desc, nameHandle, infoHandle, NULL);
  if (result != noErr) {
    ret = FALSE;
    goto done;
  }
#if DEBUG_DUMP
  GST_LOG ("ComponentDescription dump");
  gst_util_dump_mem ((const guchar *) desc, sizeof (ComponentDescription));
  gst_util_dump_mem ((gpointer) * nameHandle, 200);
  gst_util_dump_mem ((gpointer) * infoHandle, 200);
  GST_LOG ("0x%x 0x%x", **((guint8 **) nameHandle), **((guint8 **) infoHandle));
#endif

  if (*nameHandle && name) {
    gsize read, written;

    tmpname = g_strndup ((*(char **) nameHandle) + 1,
        **((guint8 **) nameHandle));
    *name = g_convert_with_fallback (tmpname, -1, "ASCII", "MAC",
        (gchar *) " ", &read, &written, NULL);
    if (!*name)
      GST_WARNING ("read:%" G_GSIZE_FORMAT ", written:%" G_GSIZE_FORMAT, read,
          written);
    g_free (tmpname);
  }

  if (*infoHandle && info) {
    tmpinfo =
        g_strndup ((*(char **) infoHandle) + 1, **((guint8 **) infoHandle));
    *info =
        g_convert_with_fallback (tmpinfo, -1, "ASCII", "MAC", (gchar *) " ",
        NULL, NULL, NULL);
    g_free (tmpinfo);
  }

done:
  DisposeHandle (nameHandle);
  DisposeHandle (infoHandle);

  return ret;
}

/*
struct CodecDecompressParams {
   ImageSequence              sequenceID;
   ImageDescriptionHandle     imageDescription;
   Ptr                        data;
   long                       bufferSize;
   long                       frameNumber;
   long                       startLine;
   long                       stopLine;
   long                       conditionFlags;
   CodecFlags                 callerFlags;
   CodecCapabilities *        capabilities;
   ICMProgressProcRecord      progressProcRecord;
   ICMCompletionProcRecord    completionProcRecord;
   ICMDataProcRecord          dataProcRecord;
   CGrafPtr                   port;
   PixMap                     dstPixMap;
   BitMapPtr                  maskBits;
   PixMapPtr                  mattePixMap;
   Rect                       srcRect;
   MatrixRecord *             matrix;
   CodecQ                     accuracy;
   short                      transferMode;
   ICMFrameTimePtr            frameTime;
   long                       reserved[1];
   SInt8                      matrixFlags;
   SInt8                      matrixType;
   Rect                       dstRect;
   UInt16                     majorSourceChangeSeed;
   UInt16                     minorSourceChangeSeed;
   CDSequenceDataSourcePtr    sourceData;
   RgnHandle                  maskRegion;
   OSType **                  wantedDestinationPixelTypes;
   long                       screenFloodMethod;
   long                       screenFloodValue;
   short                      preferredOffscreenPixelSize;
   ICMFrameTimeInfoPtr        syncFrameTime;
   Boolean                    needUpdateOnTimeChange;
   Boolean                    enableBlackLining;
   Boolean                    needUpdateOnSourceChange;
   Boolean                    pad;
   long                       unused;
   CGrafPtr                   finalDestinationPort;
   long                       requestedBufferWidth;
   long                       requestedBufferHeight;
   Rect                       displayableAreaOfRequestedBuffer;
   Boolean                    requestedSingleField;
   Boolean                    needUpdateOnNextIdle;
   Boolean                    pad2[2];
   fixed                      bufferGammaLevel;
   UInt32                     taskWeight;
   OSType                     taskName;
};
 */

/* struct ImageDescription {  */
/*     long idSize;            /\* total size of this structure *\/  */
/*  4  CodecType cType;        /\* compressor creator type *\/  */
/*  8  long resvd1;            /\* reserved--must be set to 0 *\/  */
/* 12  short resvd2;           /\* reserved--must be set to 0 *\/  */
/* 14  short dataRefIndex;     /\* reserved--must be set to 0 *\/  */
/* 16  short version;          /\* version of compressed data *\/  */
/* 18  short revisionLevel;    /\* compressor that created data *\/  */
/* 20  long vendor;            /\* compressor developer that created data *\/  */
/* 24  CodecQ temporalQuality;       */
/*                             /\* degree of temporal compression *\/  */
/* 28  CodecQ spatialQuality;        */
/*                             /\* degree of spatial compression *\/  */
/* 32  short width;            /\* width of source image in pixels *\/  */
/* 34  short height;           /\* height of source image in pixels *\/  */
/* 36  Fixed hRes;             /\* horizontal resolution of source image *\/  */
/* 40  Fixed vRes;             /\* vertical resolution of source image *\/  */
/* 44  long dataSize;          /\* size in bytes of compressed data *\/  */
/* 48  short frameCount;       /\* number of frames in image data *\/  */
/* 50  Str31 name;             /\* name of compression algorithm *\/  */
/* 82  short depth;            /\* pixel depth of source image *\/  */
/* 84  short clutID;           /\* ID number of the color table for image *\/  */
/* }; */


gboolean
get_output_info_from_component (Component componentID)
{
  gboolean ret = FALSE;
  ComponentInstance instance;
  ImageSubCodecDecompressCapabilities caps;
  CodecInfo info;

  GST_LOG ("Creating an instance");

  /* 1. Create an instance */
  if (!(instance = OpenComponent (componentID))) {
    GST_WARNING ("Couldn't open component");
    return FALSE;
  }

  /* 2. initialize */
  memset (&caps, 0, sizeof (ImageSubCodecDecompressCapabilities));
  if (ImageCodecInitialize (instance, &caps) != noErr) {
    GST_WARNING ("ImageCodecInitialize() failed");
    goto beach;
  }
#if DEBUG_DUMP
  GST_LOG ("ImageSubCodecDecompressCapabilities");
  gst_util_dump_mem ((const guchar *) &caps,
      sizeof (ImageSubCodecDecompressCapabilities));
#endif

  GST_LOG ("recordSize:%ld", caps.recordSize);
  GST_LOG ("decompressRecordSize:%ld", caps.decompressRecordSize);
  GST_LOG ("canAsync:%d", caps.canAsync);

  /* 3. Get codec info */
  memset (&info, 0, sizeof (CodecInfo));
  if (ImageCodecGetCodecInfo (instance, &info) != noErr) {
    GST_WARNING ("ImageCodecInfo() failed");
    goto beach;
  };

#if DEBUG_DUMP
  GST_LOG ("CodecInfo");
  gst_util_dump_mem ((const guchar *) &info, sizeof (CodecInfo));
#endif

  GST_LOG ("version:%d", info.version);
  GST_LOG ("revisionLevel:%d", info.revisionLevel);
  GST_LOG ("vendor:%" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (info.vendor));

  /* Compression flags */
  /* Contains flags (see below) that specify the decompression capabilities of
   * the component. Typically, these flags are of interest only to developers of
   * image decompressors. */
  GST_LOG ("decompressFlags:%lx", info.decompressFlags);
  if (info.decompressFlags & codecInfoDoes1)
    GST_LOG ("Depth 1 OK");
  if (info.decompressFlags & codecInfoDoes2)
    GST_LOG ("Depth 2 OK");
  if (info.decompressFlags & codecInfoDoes4)
    GST_LOG ("Depth 4 OK");
  if (info.decompressFlags & codecInfoDoes8)
    GST_LOG ("Depth 8 OK");
  if (info.decompressFlags & codecInfoDoes16)
    GST_LOG ("Depth 16 OK");
  if (info.decompressFlags & codecInfoDoes32)
    GST_LOG ("Depth 32 OK");
  GST_LOG ("compressFlags:%lx", info.compressFlags);

  /* Format FLAGS */
  /* Contains flags (see below) that describe the possible format for compressed
   * data produced by this component and the format of compressed files that the
   * component can handle during decompression. Typically, these flags are of
   * interest only to developers of compressor components.
   */
  GST_LOG ("formatFlags:%lx", info.formatFlags);
  if (info.formatFlags & codecInfoDepth1)
    GST_LOG ("Depth 1 OK");
  if (info.formatFlags & codecInfoDepth2)
    GST_LOG ("Depth 2 OK");
  if (info.formatFlags & codecInfoDepth4)
    GST_LOG ("Depth 4 OK");
  if (info.formatFlags & codecInfoDepth8)
    GST_LOG ("Depth 8 OK");
  if (info.formatFlags & codecInfoDepth16)
    GST_LOG ("Depth 16 OK");
  if (info.formatFlags & codecInfoDepth24)
    GST_LOG ("Depth 24 OK");
  if (info.formatFlags & codecInfoDepth32)
    GST_LOG ("Depth 32 OK");
  if (info.formatFlags & codecInfoDepth33)
    GST_LOG ("Depth 33 OK");
  if (info.formatFlags & codecInfoDepth34)
    GST_LOG ("Depth 34 OK");
  if (info.formatFlags & codecInfoDepth36)
    GST_LOG ("Depth 36 OK");
  if (info.formatFlags & codecInfoDepth40)
    GST_LOG ("Depth 40 OK");
  if (info.formatFlags & codecInfoStoresClut)
    GST_LOG ("StoresClut OK");
  if (info.formatFlags & codecInfoDoesLossless)
    GST_LOG ("Lossless OK");
  if (info.formatFlags & codecInfoSequenceSensitive)
    GST_LOG ("SequenceSentitive OK");


  GST_LOG ("compressionAccuracy:%u", info.compressionAccuracy);
  GST_LOG ("decompressionAccuracy:%u", info.decompressionAccuracy);
  GST_LOG ("compressionSpeed:%d", info.compressionSpeed);
  GST_LOG ("decompressionSpeed:%d", info.decompressionSpeed);
  GST_LOG ("compressionLevel:%u", info.compressionLevel);
  GST_LOG ("minimumHeight:%d", info.minimumHeight);
  GST_LOG ("minimumWidth:%d", info.minimumWidth);

/*   /\* . Call ImageCodecPreDecompress *\/ */
/*   memset(&params, 0, sizeof(CodecDecompressParams)); */
/*   GST_LOG ("calling imagecodecpredecompress"); */
/*   if (ImageCodecPreDecompress (instance, &params) != noErr) { */
/*     GST_WARNING ("Error in ImageCodecPreDecompress"); */
/*     goto beach; */
/*   } */

/*   GST_INFO ("sequenceID : %d", params.sequenceID); */

  ret = TRUE;

beach:
  /* Free instance */
  CloseComponent (instance);
  return TRUE;
}

void
dump_avcc_atom (guint8 * atom)
{
  /* first 8 bytes : length + atom */
  GST_LOG ("version:0x%x", QT_UINT8 (atom + 8));
  GST_LOG ("Profile:%d", QT_UINT8 (atom + 9));
  GST_LOG ("Compatible profiles : 0x%x", QT_UINT8 (atom + 10));
  GST_LOG ("Level:%d", QT_UINT8 (atom + 11));
}

void
dump_image_description (ImageDescription * desc)
{
  GST_LOG ("Description %p , size:%" G_GSIZE_FORMAT, desc, desc->idSize);

#if DEBUG_DUMP
  gst_util_dump_mem ((const guchar *) desc, desc->idSize);
#endif

  GST_LOG ("cType : %" GST_FOURCC_FORMAT, QT_FOURCC_ARGS (desc->cType));
  GST_LOG ("version:%d", desc->version);
  GST_LOG ("revisionLevel:%d", desc->revisionLevel);
  GST_LOG ("vendor:%" GST_FOURCC_FORMAT, QT_FOURCC_ARGS (desc->vendor));
  GST_LOG ("temporalQuality:%lu", desc->temporalQuality);
  GST_LOG ("spatialQuality:%lu", desc->spatialQuality);
  GST_LOG ("width:%u", desc->width);
  GST_LOG ("height:%u", desc->height);
  GST_LOG ("hres:%f", desc->hRes / 65536.0);
  GST_LOG ("vres:%f", desc->vRes / 65536.0);
  GST_LOG ("dataSize:%" G_GSIZE_FORMAT, desc->dataSize);
  GST_LOG ("frameCount:%d", desc->frameCount);
  GST_LOG ("name:%.*s", desc->name[0], desc->name + 1);
  GST_LOG ("depth:%d", desc->depth);
  GST_LOG ("clutID:%d", desc->clutID);

  if (desc->idSize > sizeof (ImageDescription)) {
    guint8 *extradata = (guint8 *) desc + sizeof (ImageDescription);
    guint32 type = QT_READ_UINT32 (extradata + 4);

    GST_LOG ("Extra Data size:%lu",
        (gulong) desc->idSize - (gulong) sizeof (ImageDescription));
#if DEBUG_DUMP
    gst_util_dump_mem ((gpointer) (gulong) desc +
        (gulong) sizeof (ImageDescription),
        (gulong) desc->idSize - (gulong) sizeof (ImageDescription));
#endif
    GST_LOG ("Extra Data Type : %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (type));
    if (type == QT_MAKE_FOURCC ('a', 'v', 'c', 'C'))
      dump_avcc_atom (extradata);
  }
}

void
dump_codec_decompress_params (CodecDecompressParams * params)
{
  GST_LOG ("params %p", params);

#if DEBUG_DUMP
  gst_util_dump_mem ((const guchar *) params, sizeof (CodecDecompressParams));
#endif

  GST_LOG ("SequenceID:%ld", params->sequenceID);
  GST_LOG ("imageDescription:%p", params->imageDescription);
  GST_LOG ("data:%p", params->data);
  GST_LOG ("bufferSize:%ld", params->bufferSize);
  GST_LOG ("frameNumber:%ld", params->frameNumber);
  GST_LOG ("startLine:%ld  , StopLine:%ld", params->startLine,
      params->stopLine);
  GST_LOG ("conditionFlags:0x%lx", params->conditionFlags);
  GST_LOG ("callerFlags:0x%x", params->callerFlags);
  GST_LOG ("capabilities:%p", params->capabilities);
  GST_LOG ("port:%p", params->port);
  GST_LOG ("dstPixMap");
#if DEBUG_DUMP
  gst_util_dump_mem ((const guchar *) &params->dstPixMap, sizeof (PixMap));
#endif

  GST_LOG ("maskBits:%p", params->maskBits);
  GST_LOG ("mattePixMap:%p", params->mattePixMap);
  GST_LOG ("srcRect %d/%d/%d/%d",
      params->srcRect.top, params->srcRect.bottom,
      params->srcRect.left, params->srcRect.right);

  GST_LOG ("matrix:%p", params->matrix);
  GST_LOG ("accuracy:%ld", params->accuracy);
  GST_LOG ("transferMode:%d", params->transferMode);
  GST_LOG ("frameTime:%p", params->frameTime);
  GST_LOG ("matrixFlags:%x", params->matrixFlags);

  GST_LOG ("dstRect %d/%d/%d/%d",
      params->dstRect.top, params->dstRect.bottom,
      params->dstRect.left, params->dstRect.right);

  GST_LOG ("sourceData:%p", params->sourceData);

  if (params->wantedDestinationPixelTypes) {
    OSType *tmp;

    for (tmp = *params->wantedDestinationPixelTypes; *tmp; tmp++)
      GST_LOG ("Destination pixel %" GST_FOURCC_FORMAT, QT_FOURCC_ARGS (*tmp));
  }
}

void
addSInt32ToDictionary (CFMutableDictionaryRef dictionary, CFStringRef key,
    SInt32 numberSInt32)
{
  CFNumberRef number =
      CFNumberCreate (NULL, kCFNumberSInt32Type, &numberSInt32);
  if (!number)
    return;
  CFDictionaryAddValue (dictionary, key, number);
  CFRelease (number);
}

void
dump_cvpixel_buffer (CVPixelBufferRef pixbuf)
{
  gsize left, right, top, bottom;

  GST_LOG ("buffer %p", pixbuf);
  if (CVPixelBufferLockBaseAddress (pixbuf, 0)) {
    GST_WARNING ("Couldn't lock base adress on pixel buffer !");
    return;
  }
  GST_LOG ("Width:%" G_GSIZE_FORMAT " , Height:%" G_GSIZE_FORMAT,
      CVPixelBufferGetWidth (pixbuf), CVPixelBufferGetHeight (pixbuf));
  GST_LOG ("Format:%" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (CVPixelBufferGetPixelFormatType (pixbuf)));
  GST_LOG ("base address:%p", CVPixelBufferGetBaseAddress (pixbuf));
  GST_LOG ("Bytes per row:%" G_GSIZE_FORMAT,
      CVPixelBufferGetBytesPerRow (pixbuf));
  GST_LOG ("Data Size:%" G_GSIZE_FORMAT, CVPixelBufferGetDataSize (pixbuf));
  GST_LOG ("Plane count:%" G_GSIZE_FORMAT, CVPixelBufferGetPlaneCount (pixbuf));
  CVPixelBufferGetExtendedPixels (pixbuf, &left, &right, &top, &bottom);
  GST_LOG ("Extended pixels. left/right/top/bottom : %" G_GSIZE_FORMAT
      "/%" G_GSIZE_FORMAT "/%" G_GSIZE_FORMAT "/%" G_GSIZE_FORMAT,
      left, right, top, bottom);
  CVPixelBufferUnlockBaseAddress (pixbuf, 0);
}


// Convenience function to dispose of our audio buffers
void
DestroyAudioBufferList (AudioBufferList * list)
{
  UInt32 i;

  if (list) {
    for (i = 0; i < list->mNumberBuffers; i++) {
      if (list->mBuffers[i].mData)
        free (list->mBuffers[i].mData);
    }
    free (list);
  }
}

// Convenience function to allocate our audio buffers
AudioBufferList *
AllocateAudioBufferList (UInt32 numChannels, UInt32 size)
{
  AudioBufferList *list;

  list = (AudioBufferList *) calloc (1, sizeof (AudioBufferList));
  if (list == NULL)
    return NULL;

  list->mNumberBuffers = 1;
  list->mBuffers[0].mNumberChannels = numChannels;
  list->mBuffers[0].mDataByteSize = size;
  list->mBuffers[0].mData = malloc (size);
  if (list->mBuffers[0].mData == NULL) {
    DestroyAudioBufferList (list);
    return NULL;
  }
  return list;
}
