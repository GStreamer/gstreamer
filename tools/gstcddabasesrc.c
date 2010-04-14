% ClassName
GstCddaBaseSrc
% TYPE_CLASS_NAME
GST_TYPE_CDDA_BASE_SRC
% pkg-config
gstreamer-cdda-0.10
% includes
#include <gst/cdda/gstcddabasesrc.h>
% prototypes
static gboolean gst_replace_open (GstCddaBaseSrc * src, const gchar * device);
static void gst_replace_close (GstCddaBaseSrc * src);
static GstBuffer *gst_replace_read_sector (GstCddaBaseSrc * src, gint sector);
static gchar *gst_replace_get_default_device (GstCddaBaseSrc * src);
static gchar **gst_replace_probe_devices (GstCddaBaseSrc * src);
% declare-class
  GstcddaBaseSrc *cddabase_src_class = GST_CDDABASE_SRC (klass);
% set-methods
  cddabase_src_class-> = GST_DEBUG_FUNCPTR (gst_replace_);
% methods


static gboolean
gst_replace_open (GstCddaBaseSrc * src, const gchar * device)
{

}

static void
gst_replace_close (GstCddaBaseSrc * src)
{

}

static GstBuffer *
gst_replace_read_sector (GstCddaBaseSrc * src, gint sector)
{

}

static gchar *
gst_replace_get_default_device (GstCddaBaseSrc * src)
{

}

static gchar **
gst_replace_probe_devices (GstCddaBaseSrc * src)
{

}
% end
