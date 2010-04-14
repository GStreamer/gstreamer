% ClassName
GstBaseRTPPayload
% TYPE_CLASS_NAME
GST_TYPE_BASE_RTP_PAYLOAD
% pkg-config
gstreamer-rtp-0.10
% includes
#include <gst/rtp/gstbasertppayload.h>
% prototypes
static gboolean
gst_replace_set_caps (GstBaseRTPPayload * payload, GstCaps * caps);
static GstFlowReturn
gst_replace_handle_buffer (GstBaseRTPPayload * payload, GstBuffer * buffer);
static gboolean gst_replace_handle_event (GstPad * pad, GstEvent * event);
static GstCaps *gst_replace_get_caps (GstBaseRTPPayload * payload,
    GstPad * pad);
% declare-class
  GstBaseRTPPayload *base_rtppayload_class = GST_BASE_RTPPAYLOAD (klass);
% set-methods
  base_rtppayload_class-> = GST_DEBUG_FUNCPTR (gst_replace_);
% methods

static gboolean
gst_replace_set_caps (GstBaseRTPPayload * payload, GstCaps * caps)
{

}

static GstFlowReturn
gst_replace_handle_buffer (GstBaseRTPPayload * payload, GstBuffer * buffer)
{

}

static gboolean
gst_replace_handle_event (GstPad * pad, GstEvent * event)
{

}

static GstCaps *
gst_replace_get_caps (GstBaseRTPPayload * payload, GstPad * pad)
{

}
% end
