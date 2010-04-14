% ClassName
GstPushSrc
% TYPE_CLASS_NAME
GST_TYPE_PUSH_SRC
% pkg-config
gstreamer-base-0.10
% includes
#include <gst/base/gstpushsrc.h>
% prototypes
static GstFlowReturn gst_replace_create (GstPushSrc * src, GstBuffer ** buf);
% declare-class
  GstPushSrc *pushsrc_class = GST_PUSHSRC (klass);
% set-methods
  pushsrc_class-> = GST_DEBUG_FUNCPTR (gst_replace_);
% methods

static GstFlowReturn
gst_replace_create (GstPushSrc * src, GstBuffer ** buf)
{

}
% end
