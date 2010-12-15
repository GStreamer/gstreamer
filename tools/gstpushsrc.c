% ClassName
GstPushSrc
% TYPE_CLASS_NAME
GST_TYPE_PUSH_SRC
% pads
srcpad-simple
% pkg-config
gstreamer-base-0.10
% includes
#include <gst/base/gstpushsrc.h>
% prototypes
static GstFlowReturn gst_replace_create (GstPushSrc * src, GstBuffer ** buf);
% declare-class
  GstPushSrcClass *pushsrc_class = GST_PUSH_SRC_CLASS (klass);
% set-methods
  pushsrc_class->create = GST_DEBUG_FUNCPTR (gst_replace_create);
% methods

static GstFlowReturn
gst_replace_create (GstPushSrc * src, GstBuffer ** buf)
{

  return GST_FLOW_OK;
}
% end
