% ClassName
GstTagDemux
% TYPE_CLASS_NAME
GST_TYPE_TAG_DEMUX
% pkg-config
gstreamer-tag-0.10
% includes
#include <gst/tag/gsttagdemux.h>
% prototypes
static gboolean
gst_replace_identify_tag (GstTagDemux * demux,
    GstBuffer * buffer, gboolean start_tag, guint * tag_size);
static GstTagDemuxResult
gst_replace_parse_tag (GstTagDemux * demux,
    GstBuffer * buffer,
    gboolean start_tag, guint * tag_size, GstTagList ** tags);
static GstTagList *gst_replace_merge_tags (GstTagDemux * demux,
    const GstTagList * start_tags, const GstTagList * end_tags);
% declare-class
  GstTagdemux *tagdemux_class = GST_TAGDEMUX (klass);
% set-methods
  tagdemux_class-> = GST_DEBUG_FUNCPTR (gst_replace_);
% methods


static gboolean
gst_replace_identify_tag (GstTagDemux * demux,
    GstBuffer * buffer, gboolean start_tag, guint * tag_size)
{

}

static GstTagDemuxResult
gst_replace_parse_tag (GstTagDemux * demux,
    GstBuffer * buffer,
    gboolean start_tag, guint * tag_size, GstTagList ** tags)
{

}

static GstTagList *
gst_replace_merge_tags (GstTagDemux * demux,
    const GstTagList * start_tags, const GstTagList * end_tags)
{

}
% end
