% ClassName
GstBaseRTPDepayload
% TYPE_CLASS_NAME
GST_TYPE_BASE_RTP_DEPAYLOAD
% pkg-config
gstreamer-rtp-0.10
% includes
#include <gst/rtp/gstbasertpdepayload.h>
% prototypes
static gboolean
gst_replace_set_caps (GstBaseRTPDepayload * filter, GstCaps * caps);
static GstFlowReturn
gst_replace_add_to_queue (GstBaseRTPDepayload * filter, GstBuffer * in);
static GstBuffer *gst_replace_process (GstBaseRTPDepayload * base,
    GstBuffer * in);
static void
gst_replace_set_gst_timestamp (GstBaseRTPDepayload * filter, guint32 timestamp,
    Gst Buffer * buf);
static gboolean
gst_replace_packet_lost (GstBaseRTPDepayload * filter, GstEvent * event);
% declare-class
  GstBaseRTPDepayload *base_rtpdepayload_class = GST_BASE_RTPDEPAYLOAD (klass);
% set-methods
  base_rtpdepayload_class-> = GST_DEBUG_FUNCPTR (gst_replace_);
% methods

static gboolean
gst_replace_set_caps (GstBaseRTPDepayload * filter, GstCaps * caps)
{

}

static GstFlowReturn
gst_replace_add_to_queue (GstBaseRTPDepayload * filter, GstBuffer * in)
{

}

static GstBuffer *
gst_replace_process (GstBaseRTPDepayload * base, GstBuffer * in)
{

}

static void
gst_replace_set_gst_timestamp (GstBaseRTPDepayload * filter, guint32 timestamp,
    Gst Buffer * buf)
{

}

static gboolean
gst_replace_packet_lost (GstBaseRTPDepayload * filter, GstEvent * event)
{

}
% end
