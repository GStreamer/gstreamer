#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* include gl filters headers */
#include "gstglgraphicmaker.h"
#include "gstglvideomaker.h"

#include "gstglimagesink.h"

#define GST_CAT_DEFAULT gst_gl_gstgl_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* Register filters that make up the gstgl plugin */
static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_gl_gstgl_debug, "gstopengl", 0, "gstopengl");
 
  if (!gst_element_register (plugin, "glvideomaker",
          GST_RANK_NONE, GST_TYPE_GL_VIDEOMAKER)) {
    return FALSE;
  }
  
  if (!gst_element_register (plugin, "glgraphicmaker",
          GST_RANK_NONE, GST_TYPE_GL_GRAPHICMAKER)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glimagesink",
          GST_RANK_NONE, GST_TYPE_GLIMAGE_SINK)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "opengl",
    "OpenGL plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
