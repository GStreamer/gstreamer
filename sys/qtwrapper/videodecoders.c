/*
 * GStreamer QuickTime video decoder codecs wrapper
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

#include "qtwrapper.h"
#include "codecmapping.h"
#include "qtutils.h"
#include "imagedescription.h"

#define QTWRAPPER_VDEC_PARAMS_QDATA g_quark_from_static_string("qtwrapper-vdec-params")

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv"));

typedef struct _QTWrapperVideoDecoder QTWrapperVideoDecoder;
typedef struct _QTWrapperVideoDecoderClass QTWrapperVideoDecoderClass;

#define MAC_LOCK(qtwrapper) g_mutex_lock (qtwrapper->lock)
#define MAC_UNLOCK(qtwrapper) g_mutex_unlock (qtwrapper->lock)

struct _QTWrapperVideoDecoder
{
  GstElement parent;

  GstPad *sinkpad;
  GstPad *srcpad;

  GMutex *lock;
  ComponentInstance instance;
  CodecInfo codecinfo;
  ImageDescriptionHandle idesc;
  CodecDecompressParams *dparams;
  CodecCapabilities codeccaps;
  guint64 frameNumber;
  ICMDecompressionSessionRef decsession;
  GstFlowReturn lastret;
  guint64 outsize;
  guint width, height;
  GstClockTime last_ts;
  GstClockTime last_duration;
  GstBuffer *prevbuf;
  gboolean flushing;
  gboolean framebuffering;

  /* width/height of output buffer */
  Rect rect;
};

struct _QTWrapperVideoDecoderClass
{
  GstElementClass parent_class;

  Component component;
  guint32 componentType;
  guint32 componentSubType;

  GstPadTemplate *sinktempl;
};

typedef struct _QTWrapperVideoDecoderParams QTWrapperVideoDecoderParams;

struct _QTWrapperVideoDecoderParams
{
  Component component;
  GstCaps *sinkcaps;
};

static GstElementClass *parent_class = NULL;

static gboolean
qtwrapper_video_decoder_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn qtwrapper_video_decoder_chain (GstPad * pad,
    GstBuffer * buf);
static gboolean qtwrapper_video_decoder_sink_event (GstPad * pad,
    GstEvent * event);

static void qtwrapper_video_decoder_finalize (GObject * object);
static void decompressCb (void *decompressionTrackingRefCon,
    OSStatus result,
    ICMDecompressionTrackingFlags decompressionTrackingFlags,
    CVPixelBufferRef pixelBuffer,
    TimeValue64 displayTime,
    TimeValue64 displayDuration,
    ICMValidTimeFlags validTimeFlags, void *reserved, void *sourceFrameRefCon);

/*
 * Class setup
 */

static void
qtwrapper_video_decoder_base_init (QTWrapperVideoDecoderClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gchar *name = NULL;
  gchar *info = NULL;
  char *longname, *description;
  ComponentDescription desc;
  QTWrapperVideoDecoderParams *params;

  params = (QTWrapperVideoDecoderParams *)
      g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      QTWRAPPER_VDEC_PARAMS_QDATA);
  g_assert (params);

  get_name_info_from_component (params->component, &desc, &name, &info);

  /* Fill in details */
  longname =
      g_strdup_printf ("QTWrapper Video Decoder : %s", GST_STR_NULL (name));
  description =
      g_strdup_printf ("QTWrapper SCAudio wrapper for decoder: %s",
      GST_STR_NULL (info));
  gst_element_class_set_metadata (element_class, longname,
      "Codec/Decoder/Video", description,
      "Fluendo <gstreamer@fluendo.com>, "
      "Pioneers of the Inevitable <songbird@songbirdnest.com>");
  g_free (longname);
  g_free (description);
  g_free (name);
  g_free (info);

  klass->sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, params->sinkcaps);

  gst_element_class_add_pad_template (element_class, klass->sinktempl);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));

  /* Store class-global values */
  klass->component = params->component;
  klass->componentType = desc.componentType;
  klass->componentSubType = desc.componentSubType;
}

static void
qtwrapper_video_decoder_class_init (QTWrapperVideoDecoderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize =
      GST_DEBUG_FUNCPTR (qtwrapper_video_decoder_finalize);
}

static void
qtwrapper_video_decoder_init (QTWrapperVideoDecoder * qtwrapper)
{
  QTWrapperVideoDecoderClass *oclass;
  ImageSubCodecDecompressCapabilities capabs;

  GST_LOG ("...");
  oclass = (QTWrapperVideoDecoderClass *) (G_OBJECT_GET_CLASS (qtwrapper));

  /* 1. Create a ocmponent instance */
  if (!(qtwrapper->instance = OpenComponent (oclass->component))) {
    GST_ERROR_OBJECT (qtwrapper, "Couldn't create a component instance !");
    return;
  }

  /* 2. Initialize decoder */
  memset (&capabs, 0, sizeof (ImageSubCodecDecompressCapabilities));
  if (ImageCodecInitialize (qtwrapper->instance, &capabs) != noErr) {
    GST_ERROR_OBJECT (qtwrapper, "Couldn't initialize the QT component !");
    return;
  }

  /* 3. Get codec info */
  memset (&qtwrapper->codecinfo, 0, sizeof (CodecInfo));
  if (ImageCodecGetCodecInfo (qtwrapper->instance,
          &qtwrapper->codecinfo) != noErr) {
    GST_ERROR_OBJECT (qtwrapper, "Couldn't get Codec Information !");
    return;
  }

  /* sink pad */
  qtwrapper->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_setcaps_function (qtwrapper->sinkpad,
      GST_DEBUG_FUNCPTR (qtwrapper_video_decoder_sink_setcaps));
  gst_pad_set_chain_function (qtwrapper->sinkpad,
      GST_DEBUG_FUNCPTR (qtwrapper_video_decoder_chain));
  gst_pad_set_event_function (qtwrapper->sinkpad,
      GST_DEBUG_FUNCPTR (qtwrapper_video_decoder_sink_event));
  gst_element_add_pad (GST_ELEMENT (qtwrapper), qtwrapper->sinkpad);

  /* src pad */
  qtwrapper->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_element_add_pad (GST_ELEMENT (qtwrapper), qtwrapper->srcpad);

  qtwrapper->lock = g_mutex_new ();
}

static void
qtwrapper_video_decoder_finalize (GObject * object)
{
  QTWrapperVideoDecoder *qtwrapper;

  qtwrapper = (QTWrapperVideoDecoder *) object;

  if (qtwrapper->lock)
    g_mutex_free (qtwrapper->lock);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* fill_image_description
 * Fills an ImageDescription with codec-specific values
 *
 * Doesn't fill in the idSize, width and height.
 */

static void
fill_image_description (QTWrapperVideoDecoder * qtwrapper,
    ImageDescription * desc)
{
  QTWrapperVideoDecoderClass *oclass;

  oclass = (QTWrapperVideoDecoderClass *) (G_OBJECT_GET_CLASS (qtwrapper));

  desc->cType = oclass->componentSubType;
  desc->version = qtwrapper->codecinfo.version;
  desc->revisionLevel = qtwrapper->codecinfo.revisionLevel;
  desc->vendor = qtwrapper->codecinfo.vendor;
  desc->temporalQuality = codecMaxQuality;
  desc->spatialQuality = codecNormalQuality;
  desc->hRes = Long2Fix (72);
  desc->vRes = Long2Fix (72);
  desc->frameCount = 1;
  /* The following is a pure empiric calculation ... so there's are chances it
   * might not work. To be fixed when we can figure out what the exact value should
   * be. */
  desc->depth = 24;
  /* no color table */
  desc->clutID = -1;
}


/* new_image_description
 *
 * Create an ImageDescription for the given 'codec_data' buffer.
 */

static ImageDescription *
new_image_description (QTWrapperVideoDecoder * qtwrapper, GstBuffer * buf,
    guint width, guint height)
{
  QTWrapperVideoDecoderClass *oclass;
  ImageDescription *desc = NULL;

  oclass = (QTWrapperVideoDecoderClass *) (G_OBJECT_GET_CLASS (qtwrapper));

  if (buf) {
    GST_LOG ("buf %p , size:%d", buf, GST_BUFFER_SIZE (buf));
#if DEBUG_DUMP
    gst_util_dump_mem (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
#endif
  }

  if (!buf) {
    /* standard case, no codec data */
    desc = g_new0 (ImageDescription, 1);
    desc->idSize = sizeof (ImageDescription);
    fill_image_description (qtwrapper, desc);
  } else {
    if ((desc =
            image_description_from_codec_data (buf, oclass->componentSubType)))
      fill_image_description (qtwrapper, desc);
    else
      goto beach;
  }

  /* Fix up values */
  desc->width = width;
  desc->height = height;
  desc->hRes = Long2Fix (72);
  desc->vRes = Long2Fix (72);

  /* if we have h264, we need frame buffering */
  if ((oclass->componentSubType == QT_MAKE_FOURCC_LE ('a', 'v', 'c', '1')))
    qtwrapper->framebuffering = TRUE;
  else
    qtwrapper->framebuffering = FALSE;

beach:
  return desc;
}

/* close_decoder
 *
 * Close and free decoder
 */
#if 0
static void
close_decoder (QTWrapperVideoDecoder * qtwrapper)
{
  if (qtwrapper->idesc) {
    DisposeHandle ((Handle) qtwrapper->idesc);
    qtwrapper->idesc = NULL;
  }

  if (qtwrapper->prevbuf) {
    gst_buffer_unref (qtwrapper->prevbuf);
    qtwrapper->prevbuf = NULL;
  }

  if (qtwrapper->dparams) {
    g_free (qtwrapper->dparams);
    qtwrapper->dparams = NULL;
  }

}
#endif
/* open_decoder
 *
 * Attempt to initialize the ImageDecompressorComponent with the given
 * caps.
 *
 * Returns TRUE and fills *outcaps if the decoder was properly initialized
 * Returns FALSE if something went wrong.
 */

static gboolean
open_decoder (QTWrapperVideoDecoder * qtwrapper, GstCaps * caps,
    GstCaps ** outcaps)
{
  ImageDescription *desc;
  gint width, height;
  GstStructure *s;
  const GValue *par = NULL;
  const GValue *rate = NULL;
  const GValue *cdata = NULL;
  OSStatus status;
  gboolean res = FALSE;
  guint32 outformat;

  ICMDecompressionSessionOptionsRef sessionoptions = NULL;
  ICMDecompressionTrackingCallbackRecord cbrecord;
  CFMutableDictionaryRef pixelBufferAttributes = NULL;


  s = gst_caps_get_structure (caps, 0);

  /* 1. Extract information from incoming caps */
  if ((!gst_structure_get_int (s, "width", &width)) ||
      (!gst_structure_get_int (s, "height", &height)) ||
      (!(rate = gst_structure_get_value (s, "framerate"))))
    goto beach;
  par = gst_structure_get_value (s, "pixel-aspect-ratio");
  cdata = gst_structure_get_value (s, "codec_data");

  /* 2. Create ImageDescription */
  if (cdata) {
    GstBuffer *cdatabuf;

    cdatabuf = gst_value_get_buffer (cdata);
    desc = new_image_description (qtwrapper, cdatabuf, width, height);
  } else {
    desc = new_image_description (qtwrapper, NULL, width, height);
  }

#if DEBUG_DUMP
  dump_image_description (desc);
#endif

  /* 3.a. Create a handle to receive the ImageDescription */
  GST_LOG_OBJECT (qtwrapper,
      "Creating a new ImageDescriptionHandle of %" G_GSIZE_FORMAT " bytes",
      desc->idSize);
  qtwrapper->idesc = (ImageDescriptionHandle) NewHandleClear (desc->idSize);
  if (G_UNLIKELY (qtwrapper->idesc == NULL)) {
    GST_WARNING_OBJECT (qtwrapper,
        "Failed to create an ImageDescriptionHandle of size %" G_GSIZE_FORMAT,
        desc->idSize);
    g_free (desc);
    goto beach;
  }

  /* 3.b. Copy the ImageDescription to the handle */
  GST_LOG_OBJECT (qtwrapper,
      "Copying %" G_GSIZE_FORMAT
      " bytes from desc [%p] to *qtwrapper->video [%p]", desc->idSize, desc,
      *qtwrapper->idesc);
  memcpy (*qtwrapper->idesc, desc, desc->idSize);
  g_free (desc);

#if G_BYTE_ORDER == G_BIG_ENDIAN
  outformat = kYUVSPixelFormat;
#else
  outformat = k2vuyPixelFormat;
#endif

  /* 4. Put output pixel info in dictionnnary */
  pixelBufferAttributes =
      CFDictionaryCreateMutable (NULL, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);

  addSInt32ToDictionary (pixelBufferAttributes, kCVPixelBufferWidthKey, width);
  addSInt32ToDictionary (pixelBufferAttributes, kCVPixelBufferHeightKey,
      height);
  addSInt32ToDictionary (pixelBufferAttributes,
      kCVPixelBufferPixelFormatTypeKey, outformat);

  /* 5. fill in callback structure */

  cbrecord.decompressionTrackingCallback = decompressCb;
  cbrecord.decompressionTrackingRefCon = qtwrapper;

  /* 6. create decompressionsession */
  status = ICMDecompressionSessionCreate (NULL,
      qtwrapper->idesc,
      sessionoptions, pixelBufferAttributes, &cbrecord, &qtwrapper->decsession);

  qtwrapper->outsize = width * height * 2;
  qtwrapper->width = width;
  qtwrapper->height = height;

  if (status) {
    GST_DEBUG_OBJECT (qtwrapper,
        "Error when Calling ICMDecompressionSessionCreate : %ld", status);
    goto beach;
  }
#if G_BYTE_ORDER == G_BIG_ENDIAN
  outformat = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
#else
  outformat = GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y');
#endif

  /* 9. Create output caps */
  *outcaps = gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, outformat,
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION,
      gst_value_get_fraction_numerator (rate),
      gst_value_get_fraction_denominator (rate), NULL);
  if (par)
    gst_structure_set_value (gst_caps_get_structure (*outcaps, 0),
        "pixel-aspect-ratio", par);
  res = TRUE;

beach:
  return res;
}

static gboolean
qtwrapper_video_decoder_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  QTWrapperVideoDecoder *qtwrapper;
  gboolean ret = FALSE;
  GstCaps *othercaps = NULL;

  qtwrapper = (QTWrapperVideoDecoder *) gst_pad_get_parent (pad);

  GST_LOG_OBJECT (qtwrapper, "caps:%" GST_PTR_FORMAT, caps);

  /* Setup the decoder with the given input caps */
  if (!(open_decoder (qtwrapper, caps, &othercaps))) {
    goto beach;
  }

  ret = gst_pad_set_caps (qtwrapper->srcpad, othercaps);
  if (!ret)
    goto beach;

beach:
  if (othercaps)
    gst_caps_unref (othercaps);
  gst_object_unref (qtwrapper);
  return ret;
}

static void
decompressCb (void *decompressionTrackingRefCon,
    OSStatus result,
    ICMDecompressionTrackingFlags decompressionTrackingFlags,
    CVPixelBufferRef pixelBuffer,
    TimeValue64 displayTime,
    TimeValue64 displayDuration,
    ICMValidTimeFlags validTimeFlags, void *reserved, void *sourceFrameRefCon)
{
  QTWrapperVideoDecoder *qtwrapper;
  GstBuffer *origbuf = (GstBuffer *) sourceFrameRefCon;

  qtwrapper = (QTWrapperVideoDecoder *) decompressionTrackingRefCon;

  GST_LOG_OBJECT (qtwrapper,
      "result:%d, flags:0x%x, pixelBuffer:%p, displayTime:%lld, displayDuration:%lld",
      (guint32) result, (guint32) decompressionTrackingFlags, pixelBuffer,
      displayTime, displayDuration);

  GST_LOG_OBJECT (qtwrapper,
      "validTimeFlags:0x%x, reserved:%p, sourceFrameRefCon:%p",
      (guint32) validTimeFlags, reserved, sourceFrameRefCon);

  if (decompressionTrackingFlags & kICMDecompressionTracking_ReleaseSourceData) {
    GST_LOG ("removing previous buffer : %p", origbuf);
    gst_buffer_unref (origbuf);
  }

  if (decompressionTrackingFlags & kICMDecompressionTracking_EmittingFrame)
    GST_LOG ("EMITTING FRAME");
  if (decompressionTrackingFlags & kICMDecompressionTracking_FrameDecoded)
    GST_LOG ("FRAME DECODED");
  if (decompressionTrackingFlags & kICMDecompressionTracking_FrameDropped)
    GST_LOG ("FRAME DROPPED");
  if (decompressionTrackingFlags &
      kICMDecompressionTracking_FrameNeedsRequeueing)
    GST_LOG ("FRAME NEEDS REQUEUING");

  if ((decompressionTrackingFlags & kICMDecompressionTracking_EmittingFrame)
      && pixelBuffer) {
    guint8 *addr;
    GstBuffer *outbuf;
    size_t size;
    GstClockTime outtime;

    size = CVPixelBufferGetDataSize (pixelBuffer);
    outtime = gst_util_uint64_scale (displayTime, GST_SECOND, 600);

    GST_LOG ("Got a buffer ready outtime : %" GST_TIME_FORMAT,
        GST_TIME_ARGS (outtime));

    if (qtwrapper->flushing) {
      CVPixelBufferRelease (pixelBuffer);
      goto beach;
    }

    dump_cvpixel_buffer (pixelBuffer);

    CVPixelBufferRetain (pixelBuffer);
    if (CVPixelBufferLockBaseAddress (pixelBuffer, 0))
      GST_WARNING ("Couldn't lock base adress on pixel buffer !");
    addr = CVPixelBufferGetBaseAddress (pixelBuffer);

    /* allocate buffer */
    qtwrapper->lastret =
        gst_pad_alloc_buffer (qtwrapper->srcpad, GST_BUFFER_OFFSET_NONE,
        (gint) qtwrapper->outsize, GST_PAD_CAPS (qtwrapper->srcpad), &outbuf);
    if (G_UNLIKELY (qtwrapper->lastret != GST_FLOW_OK)) {
      GST_LOG ("gst_pad_alloc_buffer() returned %s",
          gst_flow_get_name (qtwrapper->lastret));
      goto beach;
    }

    /* copy data */
    GST_LOG ("copying data in buffer from %p to %p",
        addr, GST_BUFFER_DATA (outbuf));
    if (G_UNLIKELY ((qtwrapper->width * 2) !=
            CVPixelBufferGetBytesPerRow (pixelBuffer))) {
      guint i;
      gulong realpixels;
      size_t stride;

      stride = CVPixelBufferGetBytesPerRow (pixelBuffer);
      realpixels = qtwrapper->width * 2;

      /* special copy for stride handling */
      for (i = 0; i < qtwrapper->height; i++)
        memmove (GST_BUFFER_DATA (outbuf) + realpixels * i,
            addr + stride * i, realpixels);

    } else
      memmove (GST_BUFFER_DATA (outbuf), addr, (int) qtwrapper->outsize);

    /* Release CVPixelBuffer */
    CVPixelBufferUnlockBaseAddress (pixelBuffer, 0);
    CVPixelBufferRelease (pixelBuffer);

    /* Set proper timestamp ! */
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (qtwrapper->srcpad));
    GST_BUFFER_TIMESTAMP (outbuf) = qtwrapper->last_ts;
    GST_BUFFER_DURATION (outbuf) = qtwrapper->last_duration;
    GST_BUFFER_SIZE (outbuf) = (int) qtwrapper->outsize;

    /* See if we push buffer downstream */
    if (G_LIKELY (!qtwrapper->framebuffering)) {
      GST_LOG ("No buffering needed, pushing buffer downstream");
      MAC_UNLOCK (qtwrapper);
      qtwrapper->lastret = gst_pad_push (qtwrapper->srcpad, outbuf);
      MAC_LOCK (qtwrapper);
    } else {
      /* Check if we push the current buffer or the stored buffer */
      if (!qtwrapper->prevbuf) {
        GST_LOG ("Storing buffer");
        qtwrapper->prevbuf = outbuf;
        qtwrapper->lastret = GST_FLOW_OK;
      } else if (GST_BUFFER_TIMESTAMP (qtwrapper->prevbuf) >
          GST_BUFFER_TIMESTAMP (outbuf)) {
        GST_LOG ("Newly decoded buffer is earliest, pushing that one !");
        MAC_UNLOCK (qtwrapper);
        qtwrapper->lastret = gst_pad_push (qtwrapper->srcpad, outbuf);
        MAC_LOCK (qtwrapper);
      } else {
        GstBuffer *tmp;

        tmp = qtwrapper->prevbuf;
        qtwrapper->prevbuf = outbuf;
        GST_LOG ("Stored buffer is earliest, pushing that one !");
        MAC_UNLOCK (qtwrapper);
        qtwrapper->lastret = gst_pad_push (qtwrapper->srcpad, tmp);
        MAC_LOCK (qtwrapper);
      }
    }
  } else {
    qtwrapper->lastret = GST_FLOW_OK;
  }

beach:
  return;
}

/* _chain
 *
 * Here we feed the data to the decoder and ask to decode frames.
 *
 * Known issues/questions are:
 *  * How can we be guaranteed that one frame in automatically gives one output
 *    frame ?
 *  * PTS/DTS timestamp issues. With mpeg-derivate formats, the incoming order is
 *    different from the output order.
 */

static GstFlowReturn
qtwrapper_video_decoder_chain (GstPad * pad, GstBuffer * buf)
{
  QTWrapperVideoDecoder *qtwrapper;
  GstFlowReturn ret = GST_FLOW_OK;
  ICMFrameTimeRecord frameTime = { {0} };
  OSStatus status;
  guint64 intime;

  qtwrapper = (QTWrapperVideoDecoder *) gst_pad_get_parent (pad);

  intime = gst_util_uint64_scale (GST_BUFFER_TIMESTAMP (buf), 600, GST_SECOND);

  GST_DEBUG_OBJECT (qtwrapper,
      "buffer:%p timestamp:%" GST_TIME_FORMAT " intime:%llu Size:%d", buf,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), intime,
      GST_BUFFER_SIZE (buf));

  frameTime.recordSize = sizeof (ICMFrameTimeRecord);
/*   *(TimeValue64 *)&frameTime.value = intime; */
  frameTime.value.lo = (guint32) (intime & 0xffffffff);
  frameTime.value.hi = (guint32) (intime >> 32);
  frameTime.base = 0;
  frameTime.scale = 600;
  frameTime.rate = fixed1;
  frameTime.duration = 1;
  frameTime.flags = icmFrameTimeDecodeImmediately;
/*   frameTime.flags = icmFrameTimeIsNonScheduledDisplayTime; */
  frameTime.frameNumber = (long) (++qtwrapper->frameNumber);

  MAC_LOCK (qtwrapper);

  qtwrapper->last_ts = GST_BUFFER_TIMESTAMP (buf);
  qtwrapper->last_duration = GST_BUFFER_DURATION (buf);

  status = ICMDecompressionSessionDecodeFrame (qtwrapper->decsession,
      GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf), NULL, &frameTime, buf);
  MAC_UNLOCK (qtwrapper);

  if (status) {
    GST_WARNING_OBJECT (qtwrapper, "Error when Calling DecodeFrame() : %ld",
        status);
    ret = GST_FLOW_ERROR;
    goto beach;
  }

beach:
  gst_object_unref (qtwrapper);
  return qtwrapper->lastret;
}

static gboolean
qtwrapper_video_decoder_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  QTWrapperVideoDecoder *qtwrapper;

  qtwrapper = (QTWrapperVideoDecoder *) gst_pad_get_parent (pad);

  GST_LOG_OBJECT (pad, "event : %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      MAC_LOCK (qtwrapper);
      qtwrapper->flushing = TRUE;
      if (qtwrapper->prevbuf) {
        GST_LOG ("About to unref buffer %p", qtwrapper->prevbuf);
        gst_buffer_unref (qtwrapper->prevbuf);
        qtwrapper->prevbuf = NULL;
      }
      ICMDecompressionSessionFlush (qtwrapper->decsession);
      MAC_UNLOCK (qtwrapper);
      break;
    case GST_EVENT_FLUSH_STOP:
      MAC_LOCK (qtwrapper);
      qtwrapper->flushing = FALSE;
      qtwrapper->prevbuf = NULL;
      MAC_UNLOCK (qtwrapper);
      break;
    default:
      break;
  }

  res = gst_pad_push_event (qtwrapper->srcpad, event);

  gst_object_unref (qtwrapper);
  return res;
}

/* _register
 *
 * Scan through all available Image Decompressor components to find the ones we
 * can handle and wrap in this plugin.
 */

gboolean
qtwrapper_video_decoders_register (GstPlugin * plugin)
{
  gboolean res = TRUE;
  Component componentID = NULL;
  ComponentDescription desc = {
    'imdc', 0, 0, 0, 0
  };
  GTypeInfo typeinfo = {
    sizeof (QTWrapperVideoDecoderClass),
    (GBaseInitFunc) qtwrapper_video_decoder_base_init,
    NULL,
    (GClassInitFunc) qtwrapper_video_decoder_class_init,
    NULL,
    NULL,
    sizeof (QTWrapperVideoDecoder),
    0,
    (GInstanceInitFunc) qtwrapper_video_decoder_init,
  };

  /* Find all ImageDecoders ! */
  GST_DEBUG ("There are %ld decompressors available", CountComponents (&desc));

  /* loop over ImageDecoders */
  do {
    componentID = FindNextComponent (componentID, &desc);

    GST_LOG ("componentID : %p", componentID);

    if (componentID) {
      ComponentDescription thisdesc;
      gchar *name = NULL, *info = NULL;
      GstCaps *caps = NULL;
      gchar *type_name = NULL;
      GType type;
      QTWrapperVideoDecoderParams *params = NULL;

      if (!(get_name_info_from_component (componentID, &thisdesc, &name,
                  &info)))
        goto next;

      if (!get_output_info_from_component (componentID)) {
        GST_WARNING ("Couldn't get output info from component");
        goto next;
      }

      GST_LOG (" name:%s", GST_STR_NULL (name));
      GST_LOG (" info:%s", GST_STR_NULL (info));

      GST_LOG (" type:%" GST_FOURCC_FORMAT,
          QT_FOURCC_ARGS (thisdesc.componentType));
      GST_LOG (" subtype:%" GST_FOURCC_FORMAT,
          QT_FOURCC_ARGS (thisdesc.componentSubType));
      GST_LOG (" manufacturer:%" GST_FOURCC_FORMAT,
          QT_FOURCC_ARGS (thisdesc.componentManufacturer));

      if (!(caps = fourcc_to_caps (thisdesc.componentSubType))) {
        GST_LOG
            ("We can't find caps for this component, switching to the next one !");
        goto next;
      }

      type_name = g_strdup_printf ("qtwrappervideodec_%" GST_FOURCC_FORMAT,
          QT_FOURCC_ARGS (thisdesc.componentSubType));
      g_strdelimit (type_name, " ", '_');

      if (g_type_from_name (type_name)) {
        GST_WARNING ("We already have a registered plugin for %s", type_name);
        goto next;
      }

      params = g_new0 (QTWrapperVideoDecoderParams, 1);
      params->component = componentID;
      params->sinkcaps = gst_caps_ref (caps);

      GST_INFO ("Registering g_type for type_name: %s", type_name);
      type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
      /* Store params in type qdata */
      g_type_set_qdata (type, QTWRAPPER_VDEC_PARAMS_QDATA, (gpointer) params);

      /* register type */
      if (!gst_element_register (plugin, type_name, GST_RANK_MARGINAL, type)) {
        g_warning ("Failed to register %s", type_name);;
        g_type_set_qdata (type, QTWRAPPER_VDEC_PARAMS_QDATA, NULL);
        g_free (params);
        res = FALSE;
        goto next;
      } else {
        GST_LOG ("Reigstered video plugin %s", type_name);
      }

    next:
      if (name)
        g_free (name);
      if (info)
        g_free (info);
      if (type_name)
        g_free (type_name);
      if (caps)
        gst_caps_unref (caps);
    }

  } while (componentID && res);

  return res;
}
