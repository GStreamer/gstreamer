#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglbuffer.h"

static GObjectClass* gst_gl_buffer_parent_class;

static void
gst_gl_buffer_finalize (GstGLBuffer* buffer)
{
    //wait clear textures end, blocking call
    gst_gl_display_clearTexture (buffer->display, buffer->texture,
	    buffer->texture_u, buffer->texture_v);

    g_object_unref (buffer->display);

    GST_MINI_OBJECT_CLASS (gst_gl_buffer_parent_class)->
	    finalize (GST_MINI_OBJECT (buffer));
}

static void
gst_gl_buffer_init (GstGLBuffer* buffer, gpointer g_class)
{
    buffer->display = NULL;
    buffer->video_format = 0;
    buffer->texture = 0;
    buffer->texture_u = 0;
    buffer->texture_v = 0;
    buffer->width = 0;
    buffer->height = 0;
}

static void
gst_gl_buffer_class_init (gpointer g_class, gpointer class_data)
{
    GstMiniObjectClass* mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

    gst_gl_buffer_parent_class = g_type_class_peek_parent (g_class);

    mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
        gst_gl_buffer_finalize;
}


GType
gst_gl_buffer_get_type (void)
{
  static GType _gst_gl_buffer_type;

  if (G_UNLIKELY (_gst_gl_buffer_type == 0)) {
    static const GTypeInfo info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_gl_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstGLBuffer),
      0,
      (GInstanceInitFunc) gst_gl_buffer_init,
      NULL
    };
    _gst_gl_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstGLBuffer", &info, 0);
  }
  return _gst_gl_buffer_type;
}


GstGLBuffer*
gst_gl_buffer_new_from_video_format (GstGLDisplay* display,
    GstVideoFormat video_format, gint context_width, gint context_height, 
    gint width, gint height)
{
    GstGLBuffer *buffer;

    g_return_val_if_fail (video_format != GST_VIDEO_FORMAT_UNKNOWN, NULL);
    g_return_val_if_fail (display != NULL, NULL);
    g_return_val_if_fail (width > 0, NULL);
    g_return_val_if_fail (height > 0, NULL);

    buffer = (GstGLBuffer *) gst_mini_object_new (GST_TYPE_GL_BUFFER);

    buffer->display = g_object_ref (display);
    buffer->width = width;
    buffer->height = height;
    buffer->video_format = video_format;
    GST_BUFFER_SIZE (buffer) = gst_gl_buffer_format_get_size (video_format, context_width, context_height);

    //blocking call, init texture 
    gst_gl_display_textureRequested (buffer->display, buffer->video_format, 
	    buffer->width, buffer->height, 
	    &buffer->texture, &buffer->texture_u, &buffer->texture_v) ;

    return buffer;
}


int
gst_gl_buffer_format_get_size (GstVideoFormat format, int width, int height)
{
  /* this is not strictly true, but it's used for compatibility with
   * queue and BaseTransform */
  return width * height * 4;
}

gboolean
gst_gl_buffer_format_parse_caps (GstCaps * caps, GstVideoFormat * format,
    gint* width, gint* height)
{
    GstStructure *structure;
    gboolean ret;

    structure = gst_caps_get_structure (caps, 0);

    if (!gst_structure_has_name (structure, "video/x-raw-gl"))
	    return FALSE;

    ret = gst_structure_get_int (structure, "width", width);
    ret &= gst_structure_get_int (structure, "height", height);

    return ret;
}
