% ClassName
GstBaseSrc
% TYPE_CLASS_NAME
GST_TYPE_BASE_SRC
% pkg-config
gstreamer-base-0.10
% includes
#include <gst/base/gstbasesrc.h>
% prototypes
static GstCaps *gst_replace_get_caps (GstBaseSrc * src);
static gboolean gst_replace_set_caps (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_replace_negotiate (GstBaseSrc * src);
static gboolean gst_replace_newsegment (GstBaseSrc * src);
static gboolean gst_replace_start (GstBaseSrc * src);
static gboolean gst_replace_stop (GstBaseSrc * src);
static void
gst_replace_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_replace_get_size (GstBaseSrc * src, guint64 * size);
static gboolean gst_replace_is_seekable (GstBaseSrc * src);
static gboolean gst_replace_unlock (GstBaseSrc * src);
static gboolean gst_replace_event (GstBaseSrc * src, GstEvent * event);
static GstFlowReturn
gst_replace_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf);
static gboolean gst_replace_do_seek (GstBaseSrc * src, GstSegment * segment);
static gboolean gst_replace_query (GstBaseSrc * src, GstQuery * query);
static gboolean gst_replace_check_get_range (GstBaseSrc * src);
static void gst_replace_fixate (GstBaseSrc * src, GstCaps * caps);
static gboolean gst_replace_unlock_stop (GstBaseSrc * src);
static gboolean
gst_replace_prepare_seek_segment (GstBaseSrc * src, GstEvent * seek,
    GstSegment * segment);
% declare-class
  GstBaseSrc *base_src_class = GST_BASE_SRC (klass);
% set-methods
  base_src_class-> = GST_DEBUG_FUNCPTR (gst_replace_);
% methods

static GstCaps *
gst_replace_get_caps (GstBaseSrc * src)
{

}

static gboolean
gst_replace_set_caps (GstBaseSrc * src, GstCaps * caps)
{

}

static gboolean
gst_replace_negotiate (GstBaseSrc * src)
{

}

static gboolean
gst_replace_newsegment (GstBaseSrc * src)
{

}

static gboolean
gst_replace_start (GstBaseSrc * src)
{

}

static gboolean
gst_replace_stop (GstBaseSrc * src)
{

}

static void
gst_replace_get_times (GstBaseSrc * src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{

}

static gboolean
gst_replace_get_size (GstBaseSrc * src, guint64 * size)
{

}

static gboolean
gst_replace_is_seekable (GstBaseSrc * src)
{

}

static gboolean
gst_replace_unlock (GstBaseSrc * src)
{

}

static gboolean
gst_replace_event (GstBaseSrc * src, GstEvent * event)
{

}

static GstFlowReturn
gst_replace_create (GstBaseSrc * src, guint64 offset, guint size,
    GstBuffer ** buf)
{

}

static gboolean
gst_replace_do_seek (GstBaseSrc * src, GstSegment * segment)
{

}

static gboolean
gst_replace_query (GstBaseSrc * src, GstQuery * query)
{

}

static gboolean
gst_replace_check_get_range (GstBaseSrc * src)
{

}

static void
gst_replace_fixate (GstBaseSrc * src, GstCaps * caps)
{

}

static gboolean
gst_replace_unlock_stop (GstBaseSrc * src)
{

}

static gboolean
gst_replace_prepare_seek_segment (GstBaseSrc * src, GstEvent * seek,
    GstSegment * segment)
{

}
% end
