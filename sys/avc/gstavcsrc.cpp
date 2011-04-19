/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@entropywave.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstavcsrc
 *
 * The avcsrc element captures video from an OS/X AVC Video Services
 * devices, typically a FireWire camera.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v avcsrc ! decodebin ! osxvideosink
 * ]|
 *
 * This pipeline captures from an AVC source, decodes the stream (either
 * DV or HDV), and displays the video.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

//#define ENABLE
#ifdef ENABLE
#include <AVCVideoServices/AVCVideoServices.h>
using namespace AVS;
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include "gstavcsrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_avc_src_debug_category);
#define GST_CAT_DEFAULT gst_avc_src_debug_category

/* prototypes */


static void gst_avc_src_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_avc_src_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_avc_src_dispose (GObject * object);
static void gst_avc_src_finalize (GObject * object);

static GstCaps *gst_avc_src_get_caps (GstBaseSrc * src);
static gboolean gst_avc_src_start (GstBaseSrc * src);
static gboolean gst_avc_src_stop (GstBaseSrc * src);
static gboolean gst_avc_src_is_seekable (GstBaseSrc * src);
static gboolean gst_avc_src_unlock (GstBaseSrc * src);
static gboolean gst_avc_src_event (GstBaseSrc * src, GstEvent * event);
static GstFlowReturn
gst_avc_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf);
static gboolean gst_avc_src_query (GstBaseSrc * src, GstQuery * query);
static gboolean gst_avc_src_unlock_stop (GstBaseSrc * src);

enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_avc_src_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("video/dv,systemstream=true;video/mpegts,systemstream=true,packetsize=188")
    );


/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_avc_src_debug_category, "avcsrc", 0, \
      "debug category for avcsrc element");

GST_BOILERPLATE_FULL (GstAVCSrc, gst_avc_src, GstBaseSrc,
    GST_TYPE_BASE_SRC, DEBUG_INIT);

static void
gst_avc_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_avc_src_src_template));

  gst_element_class_set_details_simple (element_class,
      "AVC Video Services Source", "Video/Source",
      "Captures DV or HDV video from Firewire port",
      "David Schleef <ds@entropywave.com>");
}

static void
gst_avc_src_class_init (GstAVCSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_avc_src_set_property;
  gobject_class->get_property = gst_avc_src_get_property;
  gobject_class->dispose = gst_avc_src_dispose;
  gobject_class->finalize = gst_avc_src_finalize;
  base_src_class->get_caps = GST_DEBUG_FUNCPTR (gst_avc_src_get_caps);
  base_src_class->start = GST_DEBUG_FUNCPTR (gst_avc_src_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR (gst_avc_src_stop);
  base_src_class->is_seekable = GST_DEBUG_FUNCPTR (gst_avc_src_is_seekable);
  base_src_class->unlock = GST_DEBUG_FUNCPTR (gst_avc_src_unlock);
  base_src_class->event = GST_DEBUG_FUNCPTR (gst_avc_src_event);
  base_src_class->create = GST_DEBUG_FUNCPTR (gst_avc_src_create);
  if (0)
    base_src_class->query = GST_DEBUG_FUNCPTR (gst_avc_src_query);
  if (0)
    base_src_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_avc_src_unlock_stop);

}

static void
gst_avc_src_init (GstAVCSrc * avcsrc, GstAVCSrcClass * avcsrc_class)
{
  gst_base_src_set_live (GST_BASE_SRC (avcsrc), TRUE);

  avcsrc->srcpad = gst_pad_new_from_static_template (&gst_avc_src_src_template,
      "src");

  avcsrc->queue = gst_atomic_queue_new (16);
  avcsrc->cond = g_cond_new ();
  avcsrc->queue_lock = g_mutex_new ();
}

void
gst_avc_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  /* GstAVCSrc *avcsrc = GST_AVC_SRC (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_avc_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  /* GstAVCSrc *avcsrc = GST_AVC_SRC (object); */

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_avc_src_dispose (GObject * object)
{
  /* GstAVCSrc *avcsrc = GST_AVC_SRC (object); */

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_avc_src_finalize (GObject * object)
{
  GstAVCSrc *avcsrc = GST_AVC_SRC (object);

  /* clean up object here */
  gst_atomic_queue_unref (avcsrc->queue);
  g_cond_free (avcsrc->cond);
  g_mutex_free (avcsrc->queue_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static GstCaps *
gst_avc_src_get_caps (GstBaseSrc * src)
{
  /* GstAVCSrc *avcsrc = GST_AVC_SRC (src); */

  return gst_caps_from_string ("video/mpegts,systemstream=true,packetsize=188");
}

#define kNumCyclesInMPEGReceiverSegment 20
#define kNumSegmentsInMPEGReceiverProgram 100

#ifdef ENABLE
void
MPEGReceiverMessageReceivedProc (UInt32 msg, UInt32 param1, UInt32 param2,
    void *pRefCon)
{

}

IOReturn
MyStructuredDataPushProc (UInt32 CycleDataCount,
    MPEGReceiveCycleData * pCycleData, void *pRefCon)
{
  GstAVCSrc *avcsrc = GST_AVC_SRC (pRefCon);

  if (avcsrc) {
    UInt32 numPackets = 0;
    for (UInt32 cycle = 0; cycle < CycleDataCount; cycle++)
      numPackets += pCycleData[cycle].tsPacketCount;
    GstBuffer *buffer;

    buffer = gst_buffer_new_and_alloc (numPackets*kMPEG2TSPacketSize);

    guint8 *data = GST_BUFFER_DATA (buffer);

    for (UInt32 cycle = 0; cycle < CycleDataCount; cycle++) {
      GST_LOG("Received cycle %lu of %lu - %lu packets (fw time %lx)",
	      cycle, CycleDataCount, pCycleData[cycle].tsPacketCount,
	      pCycleData[cycle].fireWireTimeStamp);
      for (UInt32 sourcePacket = 0; sourcePacket < pCycleData[cycle].tsPacketCount;
          sourcePacket++) {
        memcpy (data,
            pCycleData[cycle].pBuf[sourcePacket], kMPEG2TSPacketSize);
	data += kMPEG2TSPacketSize;

	avcsrc->packets_enqueued++;
      }
    }

    gst_atomic_queue_push (avcsrc->queue, buffer);

    g_mutex_lock (avcsrc->queue_lock);
    g_cond_signal (avcsrc->cond);
    g_mutex_unlock (avcsrc->queue_lock);
  }

  return 0;
}
#endif

static gboolean
gst_avc_src_start (GstBaseSrc * src)
{
  GstAVCSrc *avcsrc = GST_AVC_SRC (src);

  GST_DEBUG_OBJECT (avcsrc, "start");

  avcsrc->unlock = FALSE;

#ifdef ENABLE
  // Create a AVCDeviceController
  if (!avcsrc->pAVCDeviceController)
    CreateAVCDeviceController (&avcsrc->pAVCDeviceController);
  if (!avcsrc->pAVCDeviceController) {
    // TODO: This should never happen (unless we've run out of memory), but we should handle it cleanly anyway
    GST_ERROR ("Failed to create AVC device controller.");
    return FALSE;
  }

  GST_INFO ("Created AVC device controller.");

  if (avcsrc->deviceIndex >= CFArrayGetCount (avcsrc->pAVCDeviceController->avcDeviceArray)) {
    GST_ERROR ("Failed to find AVC device %d", avcsrc->deviceIndex);
    return FALSE;
  }

  avcsrc->pAVCDevice = (AVCDevice *)
      CFArrayGetValueAtIndex (avcsrc->pAVCDeviceController->avcDeviceArray,
      avcsrc->deviceIndex);

  if (!avcsrc->pAVCDevice) {
    GST_ERROR ("Failed to find AVC device %d", avcsrc->deviceIndex);
    return FALSE;
  }

  GST_INFO ("Found device with GUID 0x%016llX\n", avcsrc->pAVCDevice->guid);

  avcsrc->pAVCDevice->openDevice (nil, nil);

  avcsrc->pAVCDeviceStream = avcsrc->pAVCDevice->CreateMPEGReceiverForDevicePlug (0, nil,       // We'll install the structured callback later (MyStructuredDataPushProc),
      nil,
      MPEGReceiverMessageReceivedProc,
      nil,
      nil, kNumCyclesInMPEGReceiverSegment, kNumSegmentsInMPEGReceiverProgram);

  avcsrc->pAVCDeviceStream->pMPEGReceiver->registerStructuredDataPushCallback
      (MyStructuredDataPushProc,
      kNumCyclesInMPEGReceiverSegment, (void *) avcsrc);

  avcsrc->pAVCDevice->StartAVCDeviceStream (avcsrc->pAVCDeviceStream);
#endif

  return TRUE;
}

static gboolean
gst_avc_src_stop (GstBaseSrc * src)
{
  GstAVCSrc *avcsrc = GST_AVC_SRC (src);
  GstBuffer *buffer;

  GST_DEBUG_OBJECT (avcsrc, "stop");

  // Stop the stream
  avcsrc->pAVCDevice->StopAVCDeviceStream(avcsrc->pAVCDeviceStream);
  // Destroy the stream
  avcsrc->pAVCDevice->DestroyAVCDeviceStream(avcsrc->pAVCDeviceStream);
  avcsrc->pAVCDeviceStream = nil;

  // Forget about the device (don't destroy it; pAVCDeviceController manages it)
  avcsrc->pAVCDevice = nil;

  GST_DEBUG("Packets enqueued = %llu", avcsrc->packets_enqueued);
  GST_DEBUG("Packets dequeued = %llu", avcsrc->packets_dequeued);

  while ((buffer = GST_BUFFER (gst_atomic_queue_pop (avcsrc->queue))) != NULL) {
    gst_buffer_unref (buffer);
  }

  return TRUE;
}

static gboolean
gst_avc_src_is_seekable (GstBaseSrc * src)
{
  GstAVCSrc *avcsrc = GST_AVC_SRC (src);

  GST_DEBUG_OBJECT (avcsrc, "is_seekable");

  return FALSE;
}

static gboolean
gst_avc_src_unlock (GstBaseSrc * src)
{
  GstAVCSrc *avcsrc = GST_AVC_SRC (src);

  GST_DEBUG_OBJECT (avcsrc, "unlock");

  g_mutex_lock (avcsrc->queue_lock);
  avcsrc->unlock = TRUE;
  g_cond_signal (avcsrc->cond);
  g_mutex_unlock (avcsrc->queue_lock);

  return TRUE;
}

static gboolean
gst_avc_src_event (GstBaseSrc * src, GstEvent * event)
{
  GstAVCSrc *avcsrc = GST_AVC_SRC (src);

  GST_DEBUG_OBJECT (avcsrc, "event of type '%s'", GST_EVENT_TYPE_NAME(event));

  GST_DEBUG("Packets enqueued = %llu, dequeued = %llu",
	    avcsrc->packets_enqueued, avcsrc->packets_dequeued);

  return TRUE;
}

static GstFlowReturn
gst_avc_src_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf)
{
  GstAVCSrc *avcsrc = GST_AVC_SRC (src);
  GstBuffer *buffer;

  GST_DEBUG_OBJECT (avcsrc, "create");

  g_mutex_lock (avcsrc->queue_lock);
  buffer = GST_BUFFER (gst_atomic_queue_pop (avcsrc->queue));
  while (buffer == NULL && !avcsrc->unlock) {
    g_cond_wait (avcsrc->cond, avcsrc->queue_lock);
    buffer = GST_BUFFER (gst_atomic_queue_pop (avcsrc->queue));
  }
  g_mutex_unlock (avcsrc->queue_lock);

  if (avcsrc->unlock) {
    if (buffer)
      gst_buffer_unref (buffer);
    return GST_FLOW_WRONG_STATE;
  }

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (avcsrc->srcpad));

  *buf = buffer;

  avcsrc->packets_dequeued++;

  return GST_FLOW_OK;
}

static gboolean
gst_avc_src_query (GstBaseSrc * src, GstQuery * query)
{
  GstAVCSrc *avcsrc = GST_AVC_SRC (src);

  GST_DEBUG_OBJECT (avcsrc, "query");

  return TRUE;
}

static gboolean
gst_avc_src_unlock_stop (GstBaseSrc * src)
{
  GstAVCSrc *avcsrc = GST_AVC_SRC (src);

  GST_DEBUG_OBJECT (avcsrc, "stop");

  return TRUE;
}
