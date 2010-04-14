% ClassName
GstBaseSink
% TYPE_CLASS_NAME
GST_TYPE_BASE_SINK
% pkg-config
gstreamer-base-0.10
% includes
#include <gst/base/gstbasesink.h>
% prototypes
static GstCaps *gst_replace_get_caps (GstBaseSink * sink);
static gboolean gst_replace_set_caps (GstBaseSink * sink, GstCaps * caps);
static GstFlowReturn
gst_replace_buffer_alloc (GstBaseSink * sink, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf);
static void
gst_replace_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_replace_start (GstBaseSink * sink);
static gboolean gst_replace_stop (GstBaseSink * sink);
static gboolean gst_replace_unlock (GstBaseSink * sink);
static gboolean gst_replace_event (GstBaseSink * sink, GstEvent * event);
static GstFlowReturn
gst_replace_preroll (GstBaseSink * sink, GstBuffer * buffer);
static GstFlowReturn
gst_replace_render (GstBaseSink * sink, GstBuffer * buffer);
static GstStateChangeReturn gst_replace_async_play (GstBaseSink * sink);
static gboolean gst_replace_activate_pull (GstBaseSink * sink, gboolean active);
static void gst_replace_fixate (GstBaseSink * sink, GstCaps * caps);
static gboolean gst_replace_unlock_stop (GstBaseSink * sink);
static GstFlowReturn
gst_replace_render_list (GstBaseSink * sink, GstBufferList * buffer_list);
% declare-class
  GstBaseSink *base_sink_class = GST_BASE_SINK (klass);
% set-methods
  base_sink_class-> = GST_DEBUG_FUNCPTR (gst_replace_);
% methods


static GstCaps *
gst_replace_get_caps (GstBaseSink * sink)
{

}

static gboolean
gst_replace_set_caps (GstBaseSink * sink, GstCaps * caps)
{

}

static GstFlowReturn
gst_replace_buffer_alloc (GstBaseSink * sink, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{

}

static void
gst_replace_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{

}

static gboolean
gst_replace_start (GstBaseSink * sink)
{

}

static gboolean
gst_replace_stop (GstBaseSink * sink)
{

}

static gboolean
gst_replace_unlock (GstBaseSink * sink)
{

}

static gboolean
gst_replace_event (GstBaseSink * sink, GstEvent * event)
{

}

static GstFlowReturn
gst_replace_preroll (GstBaseSink * sink, GstBuffer * buffer)
{

}

static GstFlowReturn
gst_replace_render (GstBaseSink * sink, GstBuffer * buffer)
{

}

static GstStateChangeReturn
gst_replace_async_play (GstBaseSink * sink)
{

}

static gboolean
gst_replace_activate_pull (GstBaseSink * sink, gboolean active)
{

}

static void
gst_replace_fixate (GstBaseSink * sink, GstCaps * caps)
{

}

static gboolean
gst_replace_unlock_stop (GstBaseSink * sink)
{

}

static GstFlowReturn
gst_replace_render_list (GstBaseSink * sink, GstBufferList * buffer_list)
{

}
% end
